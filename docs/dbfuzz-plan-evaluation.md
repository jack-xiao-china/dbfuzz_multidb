# DBFuzz 整合方案评估报告

> 基于 EET (OSDI'24) 与 TxCheck (OSDI'23) 的源码深度分析和业内研究，评估当前 `dbfuzz-architecture-plan.md`（修正版 v2）的遗漏，并给出建议。

---

## 1. 方案遗漏分析

### 1.1 实现细节遗漏（高优先级）

以下机制在当前方案中被提及但未详细描述，或直接被忽略。它们在集成时如果不妥善处理，会导致功能退化或 bug。

#### ① `env_setting_stmts` — DBMS 查询优化器配置

**当前方案**：仅在 dut_base 接口中提到 `env_setting_stmts` 参数。

**实际实现（EET postgres.cc / yugabyte schema 等）**：

EET 在每次查询执行前会随机选择一组 DBMS 特定的 planner 配置，**目的是消除优化器的非确定性**：

```cpp
// postgres.cc — 40+ 个 planner 设置项
"SET enable_hashjoin = on/off"
"SET enable_nestloop = on/off"
"SET enable_incremental_sort = on/off"
"SET seq_page_cost = 1.0/0.1/10.0"
"SET geqo = on/off"
"SET jit = on/off"
"SET plan_cache_mode = auto/force_generic_plan/force_custom_plan"

// yugabyte schema
"SET yb_enable_optimizer_statistics = on"
"SET yb_enable_base_scans_cost_model = on"
```

这些设置通过 `dut->test(query, output, NULL, &env_setting_stmts)` 在查询前注入。

**遗漏风险**：不移植此机制，PostgreSQL 和 YugaByte 的 EET 模式会产生大量假阳性（优化器选择不同执行计划导致结果不同）。

**建议**：在 schema 层添加 `supported_setting` 字段（EET 已有），并在 `generate_database()` 和 `qcn_test()` 中统一应用。

---

#### ② `printed_expr` — 变换循环引用规避

**当前方案**：完全未提及。

**实际实现（EET printed_expr.cc/hh）**：

EET 的等价变换在某些情况下会创建 AST 循环引用：
```
expr → equivalent_transform() → CASE WHEN rand_bool THEN expr' ELSE expr END
                                                    ↑ 指向同一个 expr
```

`printed_expr` 在变换前捕获表达式的字符串表示，切断循环引用：

```cpp
struct printed_expr : value_expr {
    string printed_str;  // 变换前的字符串快照
    printed_expr(prod* p, shared_ptr<value_expr> expr) {
        ostringstream sstr;
        sstr << *expr;
        printed_str = sstr.str();
    }
    void out(ostream &out) { out << printed_str; }
};
```

**遗漏风险**：不实现此机制，CASE 表达式变换会导致无限递归（栈溢出）。

**建议**：必须将 `printed_expr.cc/hh` 纳入 expr 模块，并在 `bool_expr::equivalent_transform()` 的第三种变换选择中使用。

---

#### ③ De Morgan 定律变换（`bool_term::equivalent_transform()`）

**当前方案**：仅在目录结构中列出 `bool_term.hh/cc`，未描述其变换逻辑。

**实际实现（EET bool_term.cc）**：

这是 EET 的核心变换之一，应用 De Morgan 定律：
```
(a OR b) → NOT (NOT a AND NOT b)
(a AND b) → NOT (NOT a OR NOT b)
```

`back_transform()` 需要精确逆向操作：从 `NOT(bool_term)` 中提取 `not_expr` 子节点，恢复原始操作符和左右操作数。

**遗漏风险**：如果 `back_transform()` 实现不完整，最小化过程无法正确还原表达式。

**建议**：确保 `bool_term.cc` 的 `equivalent_transform()` 和 `back_transform()` 完整移植，且与 `not_expr` 的变换保持兼容。

---

#### ④ 假阳性二次验证（Re-validation）

**当前方案**：未提及。

**实际实现（EET qcn_select_tester.cc:283-300）**：

EET 在检测到潜在 bug 后，会重新执行两个查询进行验证：
```cpp
if (qit_query_result != original_query_result) {
    // 重新执行验证
    execute_query(original_query, original_query_result);
    execute_query(qit_query, qit_query_result);
    
    if (qit_query_result != original_query_result) {
        // 二次确认 → 真 bug
    } else {
        // 假阳性 → 跳过
    }
}
```

**遗漏风险**：不验证会导致大量假阳性 bug 报告（DBMS 非确定性因素：优化器随机性、并发竞争等）。

**建议**：在所有三种模式（txcheck/eet/cross）的 bug 检测路径中添加二次验证步骤。

---

#### ⑤ 递归重试机制（`retry_block_stmt` 的递归特性）

**当前方案**：提到阻塞调度但未描述递归重试细节。

**实际实现（TxCheck transaction_test.cc:342-433）**：

阻塞调度的核心是**在事务 commit/abort 后递归重试被阻塞的语句**：

```cpp
void trans_test_unit(...) {
    // 执行语句...
    if (is_commit || is_abort) {
        retry_block_stmt(stmt_pos, status_queue, debug_mode);  // 递归！
    }
}
```

递归语义：当某事务 commit/abort 释放锁后，其他被阻塞事务的语句可以立即重试。这确保了尽可能多的语句被执行，减少被 SPACE_HOLDER 替换的语句数量。

此外，`trans_test_unit()` 返回值含义：
- `2`: 致命错误/跳过 → 替换为 SPACE_HOLDER
- `1`: 成功执行 → 更新 real queues
- `0`: 仍被阻塞 → 标记 `is_blocked = true`

**遗漏风险**：不实现递归重试，阻塞调度的语句恢复率会大幅降低。

**建议**：完整移植 `retry_block_stmt()` 的递归逻辑，保持返回值语义一致。

---

#### ⑥ `infer_instrument_after_blocking()` — 插桩数量一致性

**当前方案**：完全未提及。

**实际实现（TxCheck transaction_test.cc:971-1042）**：

阻塞调度会将部分语句替换为 SPACE_HOLDER，导致插桩语句数量与实际语句数量不匹配。此函数通过 `INSTRUMENT_DEPEND` 边查找每个被替换语句的插桩组大小，并插入对应数量的占位符来保持队列一致性：

```cpp
auto set_size = instrumented_set.size();  // 原语句有 N 个插桩语句
for (int k = 0; k < set_size; k++) {
    final_after_stmt_queue.push_back(after_stmt_queue[i]);  // 填充 N 个占位
}
```

**遗漏风险**：不实现此函数，`multi_stmt_round_test()` 的迭代逻辑会因为队列长度不匹配而崩溃。

**建议**：作为 TxCheck 模块的关键函数，必须在 `transaction_test.cc` 中完整移植。

---

#### ⑦ 多轮测试的状态恢复

**当前方案**：提到 `multi_stmt_round_test()` 但未描述每轮结束后的状态恢复。

**实际实现（TxCheck transaction_test.cc:1204-1211）**：

每轮测试结束后，必须恢复原始的事务状态：
```cpp
stmt_queue = init_stmt_queue;
stmt_use = init_stmt_usage;
tid_queue = init_tid_queue;
// 恢复每个事务的原始语句和状态
for (int tid = 0; tid < trans_num; tid++) {
    trans_arr[tid].stmts = init_trans_arr[tid].stmts;
    change_txn_status(tid, init_txn_status[tid]);
}
```

**核心设计**：每轮测试一个**不同的**拓扑排序路径。通过从依赖图中删除已测试路径的节点，探索多条路径以最大化 bug 检测率。

**遗漏风险**：不恢复状态，第二轮及以后的测试会基于被修改过的队列执行，结果不可靠。

**建议**：在 `multi_stmt_round_test()` 中保存 init_* 快照，每轮结束时恢复。

---

### 1.2 DBMS 适配遗漏（中优先级）

#### ⑧ DBMS 特定语法适配

**当前方案**：提到 "合合 grammar.cc" 但未列出 DBMS 特定语法差异。

| DBMS | 语法特殊性 | 代码位置 |
|------|-----------|----------|
| **ClickHouse** | `ALTER TABLE ... DELETE` 替代 `DELETE FROM`；`ALTER TABLE ... UPDATE` 替代 `UPDATE`；RHS JOIN 不能嵌套 | grammar.cc:686, 918, 238 |
| **SQLite** | 隐藏 `rowid` 列，INSERT/INDEX 需排除；不支持 `RIGHT OUTER JOIN`、`FULL OUTER JOIN` | sqlite.cc:159; grammar.cc:729 |
| **CockroachDB** | 表分区（LIST/RANGE）；`PARTITION BY NOTHING` | grammar.cc:1228-1311 |
| **YugaByte** | `SPLIT INTO N TABLETS` 表选项 | grammar.cc:1313-1349 |
| **TiDB** | `PRE_SPLIT_REGIONS`、`AUTO_ID_CACHE` 表选项 | tidb.cc:153-156 |
| **MySQL** | 仅支持 `UNION` (无 INTERSECT/EXCEPT) | mysql.cc compound_operators |
| **PostgreSQL** | 全量复合运算符（UNION/INTERSECT/EXCEPT + ALL 变体） | postgres.cc compound_operators |

**遗漏风险**：grammar.cc 合并时如果忽略 DBMS 特定分支，会导致生成的 SQL 在特定 DBMS 上语法错误。

**建议**：在 grammar.cc 中使用 `if (schema::target_dbms == "xxx")` 条件分支，保持所有 DBMS 特定适配。

---

#### ⑨ DBMS 特定错误模式目录

**当前方案**：完全未提及。

**实际实现**：每个 DBMS 的 DUT 实现都有 15-25 个正则表达式用于区分"预期错误"和"非预期错误"：

```cpp
// MySQL — 20+ 个预期错误模式
static regex e_dup_entry("Duplicate entry[\\s\\S]*for key[\\s\\S]*");
static regex e_large_results("Result of[\\s\\S]*was larger than max_allowed_packet");
static regex e_timeout("Query execution was interrupted");
static regex e_deadlock("Deadlock found when trying to get lock");
// ...

// SQLite — 15+ 个模式
// PostgreSQL — 从 pgsqlerr.txt 文件加载
```

当触发**非预期错误**时（如 SIGSEGV），工具会保存触发语句并 abort 进程。

**遗漏风险**：不统一错误模式目录，某些 DBMS 的正常错误会被误判为 crash。

**建议**：在 schema/DUT 层统一维护每个 DBMS 的预期错误正则列表。

---

#### ⑩ DBMS 特定 Tester 权重

**当前方案**：未提及。

**实际实现（EET qcn.cc:320-343）**：

ClickHouse 的 UPDATE/DELETE 有非确定性行为，因此减少 tester 选择范围：
```cpp
if (db_schema->target_dbms == "clickhouse")
    choices = 6;  // 仅 SELECT 和 CTE tester
else
    choices = 12; // 全部 tester 类型
```

**遗漏风险**：ClickHouse 上运行 UPDATE/DELETE tester 会产生假阳性。

**建议**：在 `eet_main.cc` 或 `qcn.cc` 中保留 DBMS 特定的 tester 权重逻辑。

---

### 1.3 架构设计遗漏（中优先级）

#### ⑪ `txn_mode` 参数对 SQL 生成的约束

**当前方案**：未提及。

TxCheck 的 `txn_statement_factory()` 使用 `txn_mode=true` 参数来约束 SQL 生成：

```cpp
if (txn_mode == true) {
    use_group = 0;           // 禁止 GROUP BY
    from_clause(p, true);    // 单表 FROM（无 JOIN）
    // SELECT * 全列输出
}
```

**设计原因**：事务测试需要追踪行级读写集，多表 JOIN 和 GROUP BY 会使插桩语义变得极其复杂。

**遗漏风险**：如果 grammar.cc 合并后 TxCheck 模式不设置 `txn_mode`，生成的 SQL 会包含 JOIN/GROUP BY，导致插桩失败。

**建议**：保留 `txn_mode` 参数，在 TxCheck/Cross 模式的语句生成中强制启用。

---

#### ⑫ `write_op_id` 和 `row_id` 全局计数器

**当前方案**：提到 wkey/pkey 列但未描述其值的管理。

**实际实现（TxCheck grammar.cc:19-22）**：

```cpp
int write_op_id = 0;      // 写操作版本号，从 0 开始
static int row_id = 10000; // 行主键标识，从 10000 开始

// INSERT 时
row_id += 1000;  // 每个 INSERT 递增 1000
// UPDATE/INSERT 时
wkey = write_op_id;  // 记录当前写操作版本
write_op_id++;
```

`write_op_id` 在数据库生成后被写入 `wkey.txt`（`transfuzz.cc:86`），跨测试轮次保持连续。

**遗漏风险**：不管理这些计数器，TxCheck 的依赖分析会因缺少版本标识而失效。

**建议**：在 `generate_database()` 和 `gen_stmts_for_one_txn()` 中维护全局计数器，确保跨轮次连续。

---

#### ⑬ `SPACE_HOLDER_STMT` 的多场景使用

**当前方案**：仅在阻塞调度中提到。

**实际使用场景**：
1. **阻塞替换**：被阻塞的语句 → SPACE_HOLDER
2. **死锁处理**：死锁事务的未执行语句 → SPACE_HOLDER（代码已禁用但逻辑存在）
3. **图去环**：不在最长路径上的非必要语句 → SPACE_HOLDER
4. **精化阶段**：`refine_stmt_queue()` 将非路径语句替换为 SPACE_HOLDER

```cpp
#define SPACE_HOLDER_STMT "select 1 from (select 1) as subq_0 where 0 <> 0"
```

此语句返回空结果，不影响数据库状态，但保持事务结构完整。

**遗漏风险**：不统一定义和使用 SPACE_HOLDER，TxCheck 模式的多个子系统会不兼容。

**建议**：在 `transaction_test.hh` 中统一定义 SPACE_HOLDER_STMT 宏。

---

#### ⑭ 交叉模式的多轮状态管理

**当前方案**：描述了交叉模式的基本流程，但缺少状态管理细节。

**遗漏问题**：
1. 交叉模式需要在**同一数据库状态**上执行三次：事务执行、非事务执行原始路径、非事务执行变换路径
2. 三次执行之间如何重置数据库状态？（`dut_reset_to_backup()`）
3. 非事务执行时写入语句的 wkey/pkey 如何处理？（保持原样，不做变换）
4. 三次执行的结果如何精确对齐比较？

**建议**：在 `cross_main.cc` 中明确状态管理流程：
```
backup → 事务执行 → restore → 非事务原始执行 → restore → 非事务变换执行 → 三路比较
```

---

#### ⑮ 注释掉的异常检查（G2-item, G-SIa, G-SIb）

**当前方案**：未提及如何处理这些已实现但被禁用的检查。

**实际情况**：TxCheck 代码中 G2-item、G-SIa、G-SIb 的完整实现存在但被注释掉。原因是 VSR 虚假谓词依赖会导致这些检查产生误报。

**建议**：
- 短期：保持注释状态
- 长期：考虑引入更精确的谓词依赖追踪（如仅 SELECT 与目标语句相关的表的行子集而非全表），以减少虚假依赖并重新启用这些检查
- 在方案中明确标注这些检查的状态和未来计划

---

### 1.4 工程实践遗漏（低优先级但重要）

#### ⑯ CI/CD 与 Testcontainers

**当前方案**：Phase 0.8 提到"建立 GitHub Actions CI"，但仅编译检查，无集成测试。

**业内最佳实践**（2024-2025）：
- 使用 **Testcontainers** 或 **Docker Compose** 在 CI 中启动真实 DBMS 实例
- GitHub Actions matrix 策略并行测试多个 DBMS
- 已知 bug 回归测试（每发现一个 bug 就添加一个回归测试用例）

**建议**：
```yaml
# .github/workflows/test.yml
jobs:
  test:
    strategy:
      matrix:
        dbms: [mysql:8.0, mariadb:10.11, tidb:7.5]
    steps:
      - docker-compose up ${{ matrix.dbms }}
      - cmake --build build
      - ./dbfuzz --mode=txcheck --${{ matrix.dbms }}-db=test --seed=42 --max-rounds=100
```

---

#### ⑰ 回归测试框架

**当前方案**：完全未提及。

**问题**：TxCheck 和 EET 都没有自动化回归测试。每次修改代码后，只能手动运行工具并观察是否有 crash 或假阳性。

**建议**：
1. 建立 `tests/` 目录，包含已知 bug 的复现用例
2. 每发现一个新 bug，自动添加回归测试（`bug_N_reproduce.sh`）
3. CI 中运行所有回归测试确保修复不回退

---

#### ⑱ 统一 Docker Compose 配置

**当前方案**：Phase 4.2 提到 "Docker 测试脚本（9个DBMS）"，但沿用了原来每 DBMS 独立脚本的方式。

**建议**：创建统一的 `docker-compose.yml`，使用 profiles 控制启动哪些 DBMS：
```yaml
services:
  mysql:
    image: mysql:8.0
    profiles: [mysql, txcheck, cross]
  mariadb:
    image: mariadb:10.11
    profiles: [mariadb, txcheck]
  postgres:
    image: postgres:16
    profiles: [postgres, eet]
  # ... 其他 DBMS
```

---

## 2. 业内研究视角的补充建议

基于 2023-2026 年数据库测试领域的最新研究，以下是当前方案中未涉及但值得考虑的技术方向。

### 2.1 高优先级建议

#### ① Fucci 的随机冲突构造（RCC）— VLDB 2025

**问题**：TxCheck 的阻塞调度是**被动**应对 DBMS 锁机制，而 Fucci 主动构造事务冲突。

**RCC 核心思想**：
- 生成两个事务，使它们**必然**读写同一行数据
- 通过约束求解确保冲突存在
- 这比随机生成 + 去环的方法更高效

**对 dbfuzz 的启示**：
- 在 TxCheck 模式中，`gen_txn_stmts()` 可以增加冲突感知生成：先确定哪些行会被多个事务访问，再围绕这些行生成语句
- 这不改变现有架构，只需修改语句生成的概率分布

**集成难度**：中（需要理解 Fucci 的约束建模方式）

---

#### ② 覆盖率引导 — SQUIRREL / BUZZBEE

**问题**：TxCheck 和 EET 都是纯随机生成，没有代码覆盖率反馈。这意味着测试资源可能在已充分测试的代码路径上浪费。

**SQUIRREL 方法**：
- 使用 AFL++ 的覆盖率反馈引导 SQL 变异
- 优先选择触发新代码路径的 SQL 语句

**对 dbfuzz 的启示**：
- **短期（Phase 4）**：在 `impedance.cc` 中增加基于 SQL 特性覆盖的反馈（不是代码覆盖率，而是 SQL 语法特性覆盖率）
  - 追踪每种 SQL 特性（JOIN 类型、子查询类型、聚合函数等）被使用的次数
  - 优先选择使用频率低的特性
- **长期**：如果有 DBMS 源码访问，可集成 AFL++ 的覆盖率反馈

**集成难度**：短期低（SQL 特性覆盖），长期高（代码覆盖率）

---

#### ③ APTrans 的异常模式实例化 — ICSE 2025

**问题**：TxCheck 仅检查 6 种异常（且只启用 3 种），可能遗漏其他类型的事务异常。

**APTrans 方法**：
- 定义异常模式模板（如 "read-write-conflict"）
- 用不同的 SQL 语句和 schema 实例化模板
- 系统化覆盖更多异常类型

**对 dbfuzz 的启示**：
- 将 G2-item、G-SIa、G-SIb 从注释状态恢复，但改用**模式化检测**而非依赖 VSR
- 定义新的事务异常模式（如 cascading abort 异常、write skew 变体等）

**集成难度**：高（需要深入理解事务隔离理论）

---

### 2.2 中优先级建议

#### ④ THANOS 的存储引擎差分测试 — ICSE 2025

**问题**：同一 DBMS 的不同存储引擎（如 MySQL 的 InnoDB vs MyISAM）可能有不同的 bug。

**THANOS 方法**：
- 在 MySQL 上创建 InnoDB 表和 MyISAM 表
- 执行相同的事务序列
- 比较两种存储引擎的结果差异

**对 dbfuzz 的启示**：
- 在 EET 模式中，对同一查询在不同存储引擎上执行并比较
- 特别适合 MySQL 和 TiDB（都支持多种存储引擎）

**集成难度**：低（只需在 `create_table_stmt` 中控制 ENGINE 选项）

---

#### ⑤ DDLCheck 的 Schema 演化测试 — VLDB 2025

**问题**：TxCheck 和 EET 都假设 schema 在测试期间不变。但真实场景中，DDL 操作（ALTER TABLE、CREATE INDEX 等）可能引入 bug。

**DDLCheck 方法**：
- 构造等价的数据库（通过不同的 DDL 序列达到相同的逻辑状态）
- 在两个等价数据库上执行相同查询，比较结果

**对 dbfuzz 的启示**：
- 在 EET 模式中，`generate_database()` 可以生成不同的 DDL 序列（如先 CREATE TABLE 再 ALTER TABLE ADD COLUMN vs 一次性 CREATE TABLE 包含所有列）
- 这可以发现 DDL 优化 bug

**集成难度**：中（需要扩展 DDL 生成能力）

---

#### ⑥ SQLancer 的 NoREC/TLP 方法作为补充 oracle

**问题**：EET 的等价变换是唯一的逻辑 bug oracle。添加独立 oracle 可以交叉验证。

**NoREC（Non-Optimizing Reference Engine Construction）**：
- 关闭优化器（`SET optimizer_switch='...'`）
- 执行同一查询
- 比较优化和非优化的结果

**TLP（Ternary Logic Partitioning）**：
- 将 WHERE 条件按 TRUE/FALSE/NULL 分区
- 各分区结果合并应等于无条件查询结果

**对 dbfuzz 的启示**：
- 在 EET 模式中，除了等价变换外，还可以对同一查询应用 NoREC 或 TLP
- 两种 oracle 同时检测到不一致 → 更高置信度的 bug 报告

**集成难度**：中（需要实现 NoREC/TLP 的查询变换逻辑）

---

#### ⑦ WriteCheck 的写特定可串行化 — VLDB 2025

**问题**：TxCheck 的 oracle 基于依赖图拓扑排序，WriteCheck 基于更简单的"写特定可串行化"属性。

**WriteCheck 方法**：
- 对并发事务执行结果，检查是否存在一个只涉及写操作的串行化顺序
- 比完整的可串行化检查更简单、更快

**对 dbfuzz 的启示**：
- 在 TxCheck 模式中添加 WriteCheck 作为额外的快速 oracle
- 在依赖图分析前先用 WriteCheck 快速过滤，减少依赖分析的开销

**集成难度**：中

---

### 2.3 低优先级建议（未来方向）

#### ⑧ LLM 增强 SQL 生成

**相关工具**：QTRAN (ISSTA 2025)、FuzzySQL (arXiv 2026)、ShQveL (arXiv 2025)

**思路**：用 LLM 辅助生成更可能触发 bug 的 SQL 语句，或自动翻译 SQL 方言以支持更多 DBMS。

**对 dbfuzz 的启示**：
- 用 LLM 根据已知 bug 模式生成有针对性的 seed 语句
- 用 LLM 自动将 MySQL 方言的 SQL 翻译为 PostgreSQL 方言

**集成难度**：高（需要 LLM API 集成，且质量不稳定）

---

#### ⑨ 自底向上的 SQL 生成 — NDSS 2026

**思路**：传统 SQLsmith 是自顶向下生成（从顶层语句逐步展开），NDSS 2026 的论文提出自底向上生成，将更多资源分配给特性丰富的语法规则。

**对 dbfuzz 的启示**：
- 修改 grammar.cc 的生成策略，优先展开较少使用的 SQL 特性

**集成难度**：高（需要重新设计生成逻辑）

---

## 3. 方案修订建议清单

基于以上分析，以下是建议添加到 `dbfuzz-architecture-plan.md` 的具体修订项：

### 必须添加（P0 — 不添加会导致功能缺失）

| 编号 | 修订项 | 影响模块 | 建议位置 |
|------|--------|----------|----------|
| P0-1 | 添加 `env_setting_stmts` 机制详细描述 | schema/dut | Section 3.1 dut_base |
| P0-2 | 添加 `printed_expr` 到 expr 模块 | expr/ | Section 5 expr 策略 |
| P0-3 | 添加 De Morgan 变换和 `bool_term` 逻辑描述 | expr/bool_expr | Section 5 expr 策略 |
| P0-4 | 添加假阳性二次验证机制 | qcn_tester, cross | Section 7 交叉模式 |
| P0-5 | 添加 `retry_block_stmt` 递归重试描述 | txcheck/ | Section 2 目录结构 |
| P0-6 | 添加 `infer_instrument_after_blocking` 函数 | txcheck/ | Section 2 目录结构 |
| P0-7 | 添加多轮测试状态恢复机制 | txcheck/ | Section 6 general_process |
| P0-8 | 添加 DBMS 特定语法适配列表 | grammar/ | Section 4 grammar 策略 |
| P0-9 | 添加 `txn_mode` 参数约束描述 | grammar/ | Section 4 grammar 策略 |
| P0-10 | 添加 `write_op_id` / `row_id` 计数器管理 | core/grammar | Section 3 核心接口 |

### 应该添加（P1 — 不添加会降低质量）

| 编号 | 修订项 | 影响模块 | 建议位置 |
|------|--------|----------|----------|
| P1-1 | 添加 DBMS 错误模式目录 | schema/dut | Section 3.1 dut_base |
| P1-2 | 添加 DBMS 特定 tester 权重 | eet/ | Section 9 CLI |
| P1-3 | 添加 SPACE_HOLDER_STMT 统一定义 | txcheck/ | Section 2 目录结构 |
| P1-4 | 添加交叉模式状态管理流程 | cross/ | Section 7 交叉模式 |
| P1-5 | 明确 G2/GSI 检查的当前状态和未来计划 | txcheck/ | Section 12 风险 |
| P1-6 | 添加 CI/CD + Testcontainers 方案 | 工程 | 新增 Section |
| P1-7 | 添加回归测试框架 | 工程 | 新增 Section |
| P1-8 | 添加统一 Docker Compose 配置 | script/ | Section 2 目录结构 |
| P1-9 | 添加 `skip_one_original_execution` 优化 | eet/ | Section 6 |
| P1-10 | 添加浮点数规范化逻辑描述 | core/ | Section 6 general_process |

### 建议添加（P2 — 提升竞争力）

| 编号 | 修订项 | 来源 | 建议位置 |
|------|--------|------|----------|
| P2-1 | SQL 特性覆盖率反馈（类 impedance 扩展） | SQUIRREL 思路 | 新增 Section |
| P2-2 | 冲突感知事务生成 | Fucci RCC | Section 12 风险 → 未来方向 |
| P2-3 | 存储引擎差分测试 | THANOS | Section 12 风险 → 未来方向 |
| P2-4 | NoREC/TLP 补充 oracle | SQLancer | Section 12 风险 → 未来方向 |
| P2-5 | 异常模式实例化框架 | APTrans | Section 12 风险 → 未来方向 |

---

## 4. 修正后的实施路线建议

基于遗漏分析，建议对实施路线做以下调整：

### Phase 0 增补
- 添加 `printed_expr.cc/hh` 到 expr 模块清单
- 在 CMakeLists.txt 中确保 `printed_expr.cc` 被编译
- 添加 `env_setting_stmts` 在 dut_base 接口中的完整签名

### Phase 1 增补
- 步骤 1.1 增补：移植 `env_setting_stmts` 机制和各 DBMS 的 `supported_setting` 列表
- 步骤 1.1 增补：移植 DBMS 错误模式正则列表
- 新增步骤 1.6：移植 DBMS 特定 tester 权重逻辑（ClickHouse choices=6）
- 新增步骤 1.7：移植假阳性二次验证逻辑

### Phase 2 增补
- 步骤 2.1 增补：确保 `retry_block_stmt` 的递归重试完整移植
- 步骤 2.1 增补：确保 `infer_instrument_after_blocking` 完整移植
- 步骤 2.4 增补：确保 `txn_mode` 参数和 `write_op_id`/`row_id` 计数器管理
- 步骤 2.4 增补：确保 DBMS 特定语法分支完整保留
- 新增步骤 2.12：添加多轮测试状态恢复机制的验证
- 新增步骤 2.13：移植 G2/GSI 异常检查（保持注释状态但确保代码完整）

### Phase 3 增补
- 步骤 3.1 增补：交叉模式的完整状态管理流程（backup → 三次执行 → restore）
- 步骤 3.2 增补：三路结果精确对齐比较算法

### Phase 4 增补
- 新增步骤 4.6：CI/CD + Docker Compose 统一配置
- 新增步骤 4.7：回归测试框架（已知 bug 复现用例）
- 新增步骤 4.8：SQL 特性覆盖率反馈机制

---

## 5. 总结

### 当前方案的优点
1. ✅ 三模式架构设计合理（txcheck/eet/cross）
2. ✅ 双命名机制（wkey/vkey）解决了兼容性冲突
3. ✅ 交叉模式的"仅变换 SELECT"约束正确避免了依赖链断裂
4. ✅ CMake 构建系统选择正确
5. ✅ general_process 拆分策略合理
6. ✅ 已知 bug 修复计划明确

### 主要遗漏的风险等级

| 风险等级 | 遗漏数量 | 代表性遗漏 |
|----------|---------|-----------|
| 🔴 **功能缺失** | 10 项 | `printed_expr`、`infer_instrument`、`env_setting_stmts`、递归重试、状态恢复 |
| 🟡 **质量降低** | 10 项 | 错误模式目录、tester 权重、CI/CD、回归测试 |
| 🟢 **竞争力不足** | 5 项 | 覆盖率引导、冲突构造、存储引擎差分、LLM 增强 |

### 总体评估

当前方案覆盖了**架构层面**的关键设计决策，但在**实现层面**存在约 20 项遗漏，其中 10 项是高优先级的功能缺失。这些遗漏如果不处理，会导致：

- `printed_expr` 缺失 → CASE 变换无限递归
- `infer_instrument` 缺失 → 多轮测试崩溃
- `env_setting_stmts` 缺失 → PostgreSQL/YugaByte 假阳性
- 递归重试缺失 → 阻塞调度效率低下
- 二次验证缺失 → 大量假阳性 bug 报告

建议在开始实施前，将上述 P0 修订项补充到架构方案中，确保实现细节完整。
