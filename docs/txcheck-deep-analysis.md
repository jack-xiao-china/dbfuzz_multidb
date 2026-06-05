# TxCheck 深度分析报告

基于 OSDI'23 论文与项目源代码的对照分析，涵盖实现逻辑、核心算法、问题痛点、业内对比与 SQLsmith 增强差异。

---

## 1. 要解决的问题与痛点

### 1.1 核心问题

现有 DBMS 事务测试工具（ELLE、COBRA、POLYSI）存在两大根本缺陷：

| 痛点 | 具体表现 | 影响 |
|------|----------|------|
| **操作模式受限** | ELLE 仅允许 append 操作写入列表，无法生成 UPDATE/DELETE/复杂 INSERT | 大量深度 bug 只能被复杂 SQL 触发，简单操作模式无法覆盖 |
| **不支持谓词（Predicate）** | WHERE 子句、JOIN 条件、子查询等谓词操作的效果是隐式的（反映在中间过程而非最终结果），难以追踪 | 谓词是真实事务中最常见的特性，缺乏谓词支持导致大量 bug 被遗漏 |

### 1.2 被遗漏的 bug 类型

论文中 19 个通过 oracle 检测到的 bug，17 个使用了复杂语句+谓词，ELLE 无法生成。即使剩余 2 个不含谓词的 bug，ELLE 也只能找到其中 1 个。典型遗漏：

- **中止事务仍有副作用**（Figure 14）：T1 INSERT 后 ROLLBACK，但 MySQL 中 T0 的 UPDATE 受到 T1 影响，返回 39 行 vs 0 行差异。ELLE 无法处理 `WHERE ... NOT IN (subquery)` 这类谓词。
- **谓词匹配不一致**（Figure 1）：`UPDATE t1 SET vkey=63 WHERE c0 <= (SELECT min(vkey) FROM t0)` 与前面 SELECT 使用相同谓词但匹配了不同行集。ELLE 无法追踪谓词依赖。
- **事务内计算结果错误**（Figure 15）：单个事务内 UPDATE 后 SELECT 返回 39 行，但等价非事务执行返回 36 行。不违反任何隔离规范，ELLE 的隔离级别检查无法发现。

### 1.3 额外痛点

- 部分事务 bug **与隔离级别无关**（如 Figure 14 的中止事务污染），纯隔离级别检查方法无法发现
- 事务级依赖图可能有环，而语句级依赖图无环，使用前者会丢弃约 1/3 的有效测试用例（论文 Table 4：6/19）

---

## 2. 核心算法与实现逻辑

### 2.1 总体架构（论文 Figure 10 ↔ 代码 pipeline）

```
随机生成 → SQL 级插桩 → 阻塞调度 → 图去环 → Oracle 构建 → 结果比较
```

代码中对应 `transaction_test::test()` → `multi_stmt_round_test()` 的完整流程：

```cpp
// transaction_test.cc:1236-1285
int transaction_test::test() {
    assign_txn_id();      // 随机分配语句到事务
    assign_txn_status();  // 随机决定 commit/abort
    gen_txn_stmts();       // 用 SQLsmith 生成 SQL
    multi_stmt_round_test();  // 核心测试循环
}
```

### 2.2 语句依赖图（Statement-Dependency Graph）

**论文创新点**：将依赖粒度从事务级细化到语句级，定义 7 种语句依赖（Definition 2-8）：

| 依赖类型 | 论文定义 | 代码映射 |
|----------|----------|----------|
| stmt-item-read (WR) | Si 写入 xi，Sj 读取 xi | `WRITE_READ` — `build_WR_dependency()` |
| stmt-item-write (WW) | Si 写入 xi，Sj 写入 xi 的后继版本 | `WRITE_WRITE` — `build_WW_dependency()` |
| stmt-item-anti (RW) | Si 读取 xk，Sj 写入 xk 的后继版本 | `READ_WRITE` — `build_RW_dependency()` |
| stmt-predicate-read | Si 写入 xi ∈ Vset(P)，Sj 执行 r(P) | `VERSION_SET_DEPEND` — `build_VS_dependency()` |
| stmt-predicate-write | 谓词写依赖（覆写/版本集包含） | `OVERWRITE_DEPEND` — `build_OW_dependency()` |
| stmt-predicate-anti | Sj 覆写 Si 的谓词读 ri(P) | `OVERWRITE_DEPEND`（与 predicate-write 共用） |
| stmt-value-write | Si 的写入值由 Sj 的写入版本决定 | `VERSION_SET_DEPEND`（与 predicate-read 共用） |

**代码额外定义**（论文未明文列出但实现中存在）：

| 依赖类型 | 用途 | 代码 |
|----------|------|------|
| `INNER_DEPEND` | 同一事务内语句的顺序依赖 | `build_stmt_inner_dependency()` |
| `START_DEPEND` / `STRICT_START_DEPEND` | 事务间的时间顺序依赖（T_i 完全结束在 T_j 开始之后） | `build_start_dependency()` |
| `INSTRUMENT_DEPEND` | 插桩语句与其目标语句的绑定关系（BWR↔WRITE, AWR↔WRITE, VSR↔target） | `build_stmt_instrument_dependency()` |

**关键设计差异**：论文 Figure 4 的 SDG 只包含 5 种"实质"依赖（item-anti ×2, item-read, item-write, predicate-anti），代码实现中将 INNER/START/INSTRUMENT 也建模为边，用于：
- `INSTRUMENT_DEPEND`：确保插桩语句在去环/拓扑排序时与其目标语句一起移动（`get_instrumented_stmt_set()`）
- `INNER_DEPEND` + `START_DEPEND`：在 `topological_sort_path()` 中被删除，不参与 oracle 构建排序（论文 Theorem 1 只要求依赖顺序，不要求事务内部顺序）

### 2.3 SQL 级插桩（SQL-Level Instrumentation）

论文定义了三类插桩语句，代码 `instrumentor.cc` 完整实现了两阶段插桩：

#### Phase 1: Item-Tracking Instrumentation

```
对 UPDATE:  [BWR] → [UPDATE] → [AWR]
对 DELETE:  [BWR] → [DELETE]            （无 AWR，因为行已删除）
对 INSERT:  [INSERT] → [AWR]            （无 BWR，因为行尚不存在）
```

代码实现（`instrumentor.cc:53-128`）：

- **BWR (Before-Write Read)**：使用与目标语句相同的 predicate（`search condition`），输出即将被写入的行。对 UPDATE，复用 `update_statement->search` 生成 `query_spec`。
- **AWR (After-Write Read)**：使用 `WHERE wkey = <写入值>` 精确匹配被修改后的行。代码中通过查找 `wkey` 列索引，提取 `set_list` 中 wkey 的赋值表达式，构建等值查询。

#### Phase 2: Version-Set-Tracking Instrumentation

```
对每个语句：[VSR_0] → [VSR_1] → ... → [target_stmt]
```

代码实现（`instrumentor.cc:106-112`）：

- **VSR (Version-Set Read)**：`SELECT * FROM t_xxx`，输出目标语句引用的所有表的全部行。
- 涉及的表通过 `extract_words_begin_with(stmt_str, "t_")` 从 SQL 字本中提取表名前缀。

**与论文的关键对应**：
- 论文 Figure 5 展示的插桩结果与代码产出完全一致：VSR → BWR → stmt → AWR 的顺序
- 论文要求 PrimaryKey 和 VersionKey（代码中对应 `pkey` 和 `wkey`），每条 UPDATE/INSERT 必须设置 wkey 为新值
- 论文 Assumption 3（禁止同一 item 上同时存在 anti-dependency 和 write-dependency）在代码中无显式检查，依赖目标 DBMS 的隔离级别保证

### 2.4 阻塞调度（Blocking Scheduling）

**论文描述**：当 DBMS 的锁机制阻塞了某语句，会导致 BWR 和目标语句被分离执行，破坏插桩语义。

**代码实现**（`transaction_test.cc:1217-1233`）：

```cpp
void transaction_test::block_scheduling() {
    int round = 0;
    while (1) {
        trans_test(false);  // 按确定顺序执行
        if (stmt_queue == real_stmt_queue) break;  // 无阻塞差异，结束
        stmt_queue = real_stmt_queue;  // 用实际执行顺序替代
        stmt_use = real_stmt_usage;
        tid_queue = real_tid_queue;
        clear_execution_status();
        round++;
    }
}
```

**实现细节**（`trans_test()` + `retry_block_stmt()`）：

1. 按预定顺序逐条发送语句到 DBMS
2. 若语句被阻塞（返回 `blocked`），标记该事务为 blocked，跳过后续语句
3. 当某事务 commit/abort 后，递归调用 `retry_block_stmt()` 尝试恢复被阻塞的语句
4. 被阻塞的语句用 `SPACE_HOLDER_STMT`（`SELECT 1 FROM (SELECT 1) AS subq_0 WHERE 0 <> 0`）替代
5. 循环直到实际执行顺序与预定顺序一致（`stmt_queue == real_stmt_queue`）

**与论文 Figure 11 的对应**：论文示例中 T1.S1.BWR 和 T1.S1 被阻塞分离后，整个 BWR+WRITE 组合被删除（换成 space holder），代码实现完全一致。

### 2.5 图去环（Graph Decycling）

**论文描述**（Section 3.3 + Figure 8）：随机选择环中一个节点删除，直到图无环。

**代码实现**（`dependency_analyzer.cc:1360-1570` `topological_sort_path()`）：

代码采用了更精细的策略，**与论文描述有显著差异**：

1. **论文说"随机选择"**，代码实际选择**边数最多的节点**（`max_edge_num`）删除
2. 删除时连带删除整个插桩组（`get_instrumented_stmt_set()`），包括 VSR、BWR、AWR 及目标语句
3. 在 `topological_sort_path()` 中，如果一个节点找不到零入度节点（存在环），选择边最多的节点删除并继续
4. 在 `transaction_test::multi_stmt_round_test()` 中使用 `longest_stmt_path()` 进一步细化：
   - 先基于 `init_da`（初始依赖图的拓扑排序）获取最长路径
   - 将路径外的非必要事务改为 ABORT
   - 将路径外的非必要语句改为 SPACE_HOLDER
   - 反复迭代直到路径稳定

**`longest_stmt_path()` 的边权设计**（`dependency_analyzer.cc:1300-1358`）：

```cpp
INNER_DEPEND only           → weight = 1
STRICT_START_DEPEND only    → weight = 10
含 STRICT_START/INNER + 其他  → weight = 100
WR/WW                      → weight = 100000
RW/VS/OW                   → weight = 10000
```

权重策略优先保留 WR/WW 依赖（数据流核心），其次 VS/OW/RW（谓词依赖），最后才是内部顺序和启动依赖。这确保了去环后保留的路径包含最关键的数据依赖。

### 2.6 Oracle 构建与检查（Transactional Oracle Construction）

**论文 Theorem 1**：若 SDG(H) 无环，则拓扑排序产生的语句序列 S(SDG(H)) 无事务执行时，与 H 中每条语句产生相同结果。

**代码实现**（`transaction_test::multi_stmt_round_test()` + `normal_stmt_test()` + `check_normal_stmt_result()`）：

```
1. 多轮迭代去环，获得无环 SDG 和最长语句路径
2. 对路径中的每条语句，在无事务环境下顺序执行（normal_stmt_test）
3. 比较两种执行的结果：
   - 逐语句输出比较（compare_output）
   - 最终数据库内容比较（compare_content）
   - 错误信息比较（error info 逐条对比）
4. 任一不一致 → 报告 bug
```

**`compare_output()` 与 `compare_content()` 的实现**：
- 使用 BKDRHash 对每行输出计算哈希
- 对哈希集合排序后逐个比较（应对 DBMS 输出顺序不确定的情况）
- 浮点数规范化：`round(value * 100) / 100`（保留 2 位小数）

### 2.7 隔离级别异常检查

代码实现了论文提到的 6 种异常检查，但**当前只启用了 G1a、G1b、G1c**：

```cpp
// transaction_test.cc:206-229
if (da->check_G1a() == true) { return true; }
if (da->check_G1b() == true) { return true; }
if (da->check_G1c() == true) { return true; }
// G2_item, GSIa, GSIb 已注释掉
```

| 异常 | 代码实现 | 当前状态 |
|------|----------|----------|
| G1a (Aborted Reads) | `check_G1a()` — 检查 abort 事务的 WR 依赖到 commit 事务 | ✅ 启用 |
| G1b (Intermediate Reads) | `check_G1b()` — 在行历史中查找中间版本被其他事务读取 | ✅ 启用 |
| G1c (Circular Info Flow) | `check_G1c()` — 检查 WW+WR 边构成的环 | ✅ 启用 |
| G2-item (Anti-dependency Cycles) | `check_G2_item()` — 检查 WW+WR+RW 边构成的环 | ❌ 注释掉 |
| G-SIa (Interference) | `check_GSIa()` — WR/WW 依赖缺少 start-dependency | ❌ 注释掉 |
| G-SIb (Missed Effects) | `check_GSIb()` — 含恰好一条 RW 边的环 | ❌ 注释掉 |

G2/GSI 被注释掉的原因：论文指出 VSR 可能引入虚假谓词依赖（overcount），导致隔离检查假阳性。代码注释掉这些检查以避免误报。

---

## 3. 论文 vs 代码的关键差异

| 方面 | 论文描述 | 代码实现 | 差异原因 |
|------|----------|----------|----------|
| 去环节点选择 | "随机选择环中一个节点" | 选择**边数最多的节点** | 更高效地消除环，减少迭代轮数 |
| Oracle 生成 | 算法 1：随机选择零入度节点 | `longest_stmt_path()` + 拓扑排序 | 优先选择包含最多数据依赖的路径，提高 bug 触发率 |
| G2/GSI 检查 | 论文提到可做 | 代码注释掉 | VSR 引入虚假谓词依赖会导致隔离检查误报 |
| 事务配置 | 论文未明确 | TXN_NUM=6 (3×2), TXN_STMT_NUM=4 | 3 个并发事务 × 2（全 commit + 部分 abort），每个事务 4 条语句 |
| 测试用例最小化 | 论文未详述 | `minimize_testcase()` 先事务级再语句级 | 论文只提到 `--min` 选项，代码实现两级 delta debugging |
| 插桩表提取 | 论文概念描述 | `extract_words_begin_with(stmt, "t_")` | 实用的文本解析方法，从 SQL 字面中提取表名 |
| 行输出比较 | 论文说"结果相同" | BKDRHash + 排序比较 | 处理 DBMS 输出顺序不确定性和浮点精度差异 |

---

## 4. 与 SQLsmith 的对比与增强

### 4.1 SQLsmith 原始能力

SQLsmith 是一个随机 SQL 查询生成器，核心功能：
- 从目标 DBMS 提取 schema 信息
- 基于语法规则随机生成 SQL 语句（SELECT、INSERT、UPDATE、DELETE 等）
- 生成覆盖多种 SQL 特性的复杂查询（JOIN、子查询、聚合函数等）
- 发现 crash bug（通过 ASan 或服务器崩溃检测）
- **不支持事务测试**：每条语句独立执行，无事务上下文

### 4.2 TxCheck 对 SQLsmith 的增强

| 维度 | SQLsmith | TxCheck | 增强程度 |
|------|----------|---------|----------|
| **测试目标** | 单语句 crash/memory bug | 事务语义 bug + crash bug | 从单语句扩展到多事务交互 |
| **Oracle** | 无（仅检测 crash） | 语义等价 oracle（Theorem 1）+ 隔离级别检查 | 从无 oracle 到可证明正确性的 oracle |
| **语句生成** | 随机单语句 | 随机多事务 × 多语句，含 begin/commit/abort | 生成完整事务场景 |
| **插桩** | 无 | BWR + AWR + VSR 三类插桩 | 提取依赖信息的关键创新 |
| **依赖分析** | 无 | 7 种语句依赖图 + 环检测 | 从零到完整的依赖分析框架 |
| **测试用例精化** | 无 | 图去环 + 阻塞调度 + 最长路径 | 自动精化到可做 oracle 的状态 |
| **谓词支持** | 生成含谓词的 SQL 但不分析 | 生成 + 分析谓词依赖（VS/OW） | 从生成到生成+分析 |
| **Schema 约束** | 无特殊约束 | 要求 pkey + wkey 列 | 硦桩的技术前提 |
| **DBMS 重置** | 简单 reconnect | backup/restore 机制 | 支持事务测试的状态重置 |

### 4.3 共享的基础设施

TxCheck 直接复用 SQLsmith 的：
- `relmodel.cc/.hh` — 关系模型类型系统
- `schema.cc/.hh` — Schema 提取框架（扩展了 `schema_mysql/mariadb/tidb`）
- `grammar.cc/.hh` — SQL 语法产生式
- `expr.cc/.hh` — 表达式生成
- `prod.cc/.hh` — AST 基类
- `random.cc/.hh` — 随机数生成

### 4.4 TxCheck 新增的 ~3.5k 行代码

论文声称新增 3.5k 行（不含 DBMS 支持代码），对应：
- `transaction_test.cc/.hh` — 事务测试框架（~1200 行）
- `instrumentor.cc/.hh` — SQL 级插桩（~250 行）
- `dependency_analyzer.cc/.hh` — 依赖图构建与分析（~1800 行）
- `general_process.cc/.hh` — 部分新增功能（hash、比较、最小化、reproduce）

---

## 5. 与业内同类工具的对比

### 5.1 黑盒隔离检查器

| 工具 | 方法 | 操作约束 | 谓词支持 | 适用隔离级别 | 优势 | 不足 |
|------|------|----------|----------|-------------|------|------|
| **ELLE** (Jepsen) | 列数据结构 + 版本推断 | 仅 append | ❌ | Serializability, SI 等 | 历史恢复 O(m·p)；广泛使用 | 无法处理复杂操作和谓词 |
| **COBRA** | RMW 模式 + SMT 编码 | read-modify-write | ❌ | Serializability | 快速 SMT 验证；约束编码紧凑 | 同样受限于操作模式 |
| **POLYSI** | RMW + 并行硬件 | read-modify-write | ❌ | Snapshot Isolation | 专门优化 SI 检查；并行加速 | 仅覆盖 SI |
| **TxCheck** | 语句依赖图 + oracle 构建 | 仅要求 pkey+wkey | ✅ | PL-CS 及以上（Figure 7） | 支持复杂 SQL 和谓词；可检测非隔离 bug | VSR 可能引入虚假依赖；O(m²n) 复杂度 |

### 5.2 DBMS 逻辑/内存 bug 检测器

| 工具 | 检测目标 | Oracle | 事务支持 | 与 TxCheck 的关系 |
|------|----------|--------|----------|------------------|
| **SQLancer** (PQS/TLP/NORE) | 逻辑 bug | 差分 oracle | ❌ | 不同维度：SQLancer 检测单查询正确性，TxCheck 检测事务语义 |
| **SQUIRREL** | 内存 bug | ASan + 语义 | 部分（多语句 IR） | SQUIRREL 关注内存安全，TxCheck 关注事务正确性 |
| **DynSQL** | 内存 bug + 语义 | 状态感知生成 | ❌ | DynSQL 的增量生成思路可借鉴于 TxCheck 的语句生成 |
| **APOLLO** | 性能 bug | 性能回归检测 | ❌ | 完全不同维度 |

### 5.3 TxCheck 的独特优势

1. **谓词依赖追踪**：业内首个通过 VSR 语句追踪谓词操作效果的黑盒方法
2. **语义等价 oracle**：不依赖隔离规范定义，可发现与隔离级别无关的 bug（如中止事务污染）
3. **语句级粒度**：避免事务级依赖图丢掉 ~1/3 的有效测试用例
4. **黑盒适用**：仅使用 SQL 语句插桩，无需源码访问

### 5.4 TxCheck 的局限与风险

1. **VSR 虚假依赖**：`SELECT * FROM t` 输出全表行，可能包含未被谓词引用的行 → 虚假 VS/OW 边 → SDG 是实际依赖图的超图。论文证明这不影响 oracle 正确性（Theorem 1 对超图仍然成立），但导致 G2/GSI 检查被注释掉
2. **O(m²n) 复杂度**：比 ELLE 的 O(m·p) 更慢，但在实践中可接受（m 有限，通常 <50 语句）
3. **表结构约束**：要求 pkey + wkey 列，限制了可测试的 schema 类型
4. **无覆盖率引导**：随机生成无代码覆盖率反馈，可能遗漏边角 case（论文 Section 6 Discussion 承认此局限）
5. **顺序执行**：为可复现性选择顺序而非并发执行，可能错过某些并发 bug
6. **本地部署限制**：Assumption 1 要求无同步问题，仅适用于本地部署的 DBMS

---

## 6. 功能与适用场景差异总结

### 6.1 TxCheck 适用场景

| 场景 | TxCheck 适用性 | ELLE 适用性 |
|------|---------------|------------|
| 检测复杂谓词事务 bug | ✅ 最佳 | ❌ 不支持 |
| 检测中止事务副作用 | ✅ 可检测 | ❌ 不支持 |
| 检测事务内计算错误 | ✅ oracle 可检测 | ❌ 仅检测隔离违反 |
| 检测隔离级别违反（G1a/G1b/G1c） | ✅ 可检测 | ✅ 更专精 |
| 检测 SI 隔离违反（G-SIa/G-SIb） | ⚠️ 代码注释掉 | ✅ 专精 |
| 检测 Serializability 违反（G2） | ⚠️ 代码注释掉 | ✅ 可检测 |
| 快速大规模并发测试 | ❌ 顺序执行 | ✅ 支持并发 |
| 分布式 DBMS 测试 | ❌ Assumption 1 限制 | ✅ Jepsen 框架支持 |
| 无源码黑盒测试 | ✅ 纯 SQL | ✅ 纯黑盒 |

### 6.2 互补关系

TxCheck 和 ELLE/COBRA/POLYSI 是**互补而非替代**关系：
- TxCheck 覆盖了 ELLE 无法触及的谓词+复杂操作 bug 空间
- ELLE 在隔离级别检查方面更专精（尤其是 G2/SI 的精确检查）
- 理想的测试策略是同时使用 TxCheck（覆盖广度）+ ELLE（隔离深度）

### 6.3 与 SQLsmith 的功能差异

| 场景 | SQLsmith | TxCheck |
|------|----------|---------|
| 单语句 crash bug | ✅ 主要目标 | ✅ 附带检测 |
| 事务语义 bug | ❌ 不支持 | ✅ 核心目标 |
| 逻辑 bug（查询结果错误） | ❌ 无 oracle | ⚠️ 仅在事务上下文中可检测 |
| 压力测试 / 性能测试 | ✅ 随机生成可做压力 | ❌ 每轮需重置数据库 |
| 快速发现 crash | ✅ 更高效 | ⚠️ 每轮更重（插桩+多事务+重置） |

---

## 7. 代码实现中的关键设计洞察

### 7.1 `get_instrumented_stmt_set()` 的递归语义

此函数通过 `INSTRUMENT_DEPEND` 边递归查找一个目标语句的所有插桩语句（VSR→target, BWR→WRITE, AWR→WRITE），形成一个"插桩组"。在去环和拓扑排序时，整个组作为一个单元移动，确保插桩语义不被破坏。

### 7.2 `infer_instrument_after_blocking()` 的必要性

阻塞调度会改变语句顺序和替换部分语句为 SPACE_HOLDER，导致之前的插桩结果与调度后的语句队列不匹配。此函数通过对比 before/after 阻塞的队列，推断新的插桩对齐关系，特别是处理被 SPACE_HOLDER 替换的语句的插桩组大小调整。

### 7.3 行历史（`history`）的依赖推断机制

`dependency_analyzer` 的构造函数中，将所有语句输出按行（row_id）组织成 `change_history`，每行的 `row_op_list` 是一个操作序列。然后对每个操作调用 `build_WR/RW/WW_dependency()`，基于**同一行上的操作时序**推断依赖：

- WR：在 op_list 中向前搜索最近的 `AFTER_WRITE_READ`（hash 匹配 → 写入-读取依赖）
- WW：在 op_list 中向前搜索最近的 `AFTER_WRITE_READ`（hash 匹配 → 写入-写入依赖）
- RW：遍历整个 op_list 查找 `SELECT_READ` 或 `AFTER_WRITE_READ`（write_op_id 匹配 → 读取-写入依赖）

这与论文 Lemma 1-3 的证明逻辑完全对应。

### 7.4 `compare_content()` 的 bug

代码 `general_process.cc:415` 中 `for (auto iter = a_content.begin(); iter != a_content.begin(); iter++)` 的终止条件是 `a_content.begin()` 而非 `a_content.end()`，这意味着循环永远不会执行。这是一个实际 bug，可能导致数据库内容比较被跳过。

### 7.5 `build_VS_dependency()` 中 `set_intersection` 的 bug

代码 `dependency_analyzer.cc:284-286`：
```cpp
set_intersection(i_primary_set.begin(), i_primary_set.begin(),  // 应为 begin(), end()
    before_write_primary_set.begin(), before_write_primary_set.begin(),  // 同上
    inserter(res, res.begin()));
```
两个 `set_intersection` 调用的范围都是 `[begin, begin)`，即空范围，导致结果永远为空集。这与 DELETE 的 VS 依赖推断逻辑矛盾（应为 `[begin, end)`）。这意味着 DELETE 语句的 VS 依赖推断在代码中**实际不工作**。