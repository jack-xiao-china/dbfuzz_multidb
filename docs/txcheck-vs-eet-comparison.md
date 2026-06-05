# TxCheck vs EET 深度对比分析与集成建议

> EET (OSDI'24) 与 TxCheck (OSDI'23) 均为同一作者 Zu-Ming Jiang 在 SQLsmith 基础上的功能扩展，但解决不同维度的问题。本文分析二者差异，评估集成可行性，并给出建议。

---

## 1. 核心定位对比

| 维度 | TxCheck | EET |
|------|----------|-----|
| **论文** | OSDI'23 — "Detecting Transactional Bugs in Database Engines via Graph-Based Oracle Construction" | OSDI'24 — "Detecting Logic Bugs in Database Engines via Equivalent Expression Transformation" |
| **检测目标** | **事务语义 bug**：隔离违反、中止事务污染、事务内计算错误 | **逻辑 bug**：单条 SQL 查询/语句的结果错误 |
| **Oracle 方法** | 语句依赖图 → 拓扑排序 → 构建语义等价非事务序列 → 比较结果 | 等价表达式变换 → 生成数学等价的变体查询 → 比较结果 |
| **测试粒度** | **多事务 × 多语句**（6 个事务，每事务 4 条语句） | **单条语句**（SELECT / UPDATE / DELETE / CTE） |
| **并发模型** | 多事务按确定顺序交错执行（含阻塞调度） | 单线程，无并发，无事务 |
| **bug 类型** | 事务隔离违反(G1a/G1b/G1c)、中止事务副作用、事务与非事务结果不一致 | 查询优化器 bug、谓词评估 bug、表达式计算 bug、crash |

**一句话总结**：TxCheck 测"事务交错是否正确"，EET 测"单条 SQL 是否返回正确结果"。二者检测的 bug 空间几乎不重叠。

---

## 2. Oracle 设计的深层对比

### 2.1 TxCheck Oracle：依赖图 + 语义等价重排序

**原理**（Theorem 1）：
```
若 SDG(H) 无环，则 H 的拓扑排序序列 S(SDG(H)) 在无事务执行时
与 H 在事务执行时产生相同结果。任何差异 = bug。
```

**核心假设**：
- 操作顺序不影响无依赖的语句（论文不把顺序算作依赖）
- 依赖图无环时，拓扑排序保证了所有数据依赖被尊重
- **依赖于 DBMS 的隔离级别**（要求 PL-CS 及以上，Assumption 3）

**优势**：检测事务语义 bug 的唯一通用方法
**局限**：不检测单语句结果正确性；需要 pkey+wkey 约束；SDG 有环时需去环（可能丢掉 1/3 测试用例）

### 2.2 EET Oracle：等价表达式变换

**原理**（三值逻辑等价）：
```
原表达式 expr ≡ (rand_bool OR NOT rand_bool OR rand_bool IS NULL) AND expr
原表达式 expr ≡ (rand_bool AND NOT rand_bool AND rand_bool IS NOT NULL) OR expr
```

即：`(TRUE AND expr) = expr`，`(FALSE OR expr) = expr`。其中 TRUE/FALSE 由随机布尔表达式的三值逻辑恒等式构造。

**核心假设**：
- 三值逻辑等价是数学成立的（SQL NULL 处理已正确建模）
- 变换后的查询触发不同的优化器路径，但结果应相同
- **不依赖任何隔离级别假设**

**优势**：不依赖隔离级别；适用于任意 SQL 查询；变换可递归应用于表达式树的任意节点
**局限**：只能检测"数学上应该等价但实际不等价"的 bug；不能检测事务语义 bug

### 2.3 两种 Oracle 的本质差异

| 特性 | TxCheck Oracle | EET Oracle |
|------|---------------|------------|
| **等价来源** | 数据依赖图保证的语义等价 | 数学恒等式保证的逻辑等价 |
| **等价前提** | SDG 无环 + 隔离级别约束 | 三值逻辑正确（无需任何 DBMS 假设） |
| **等价粒度** | 整个事务执行历史 vs 重排序序列 | 单条 SQL 的原始版本 vs 变换版本 |
| **假阳性风险** | VSR 虚假依赖（但不影响 oracle 正确性） | 随机变换可能触发 DBMS 的合法限制（如 ClickHouse 不支持子查询） |
| **假阴性风险** | 随机拓扑排序可能选到与事务执行产生相同错误结果的排序（<3%概率） | 变换后查询可能走了与原查询相同的优化路径（不触发 bug） |

---

## 3. 代码架构对比

### 3.1 共同基础

两个项目都基于 SQLsmith，共享：

| 组件 | TxCheck | EET | 是否修改 |
|------|---------|-----|----------|
| `relmodel.cc/hh` | ✅ 使用 | ✅ 使用 | 基本未改 |
| `prod.cc/hh` | ✅ 使用 | ✅ 使用 | EET 添加了 `component_id` 字段 |
| `random.cc/hh` | ✅ 使用 | ✅ 使用 | 基本未改 |
| `schema.cc/hh` | ✅ 扩展(mysql/mariadb/tidb) | ✅ 扩展(mysql/pg/sqlite/clickhouse/tidb/ob/yugabyte/cockroach) | 各自扩展不同 DBMS |
| `grammar.cc/hh` | ✅ 扩展(事务语句、pkey/wkey约束) | ✅ 扩展(CTE、UNION/INTERSECT/EXCEPT、INSERT-SELECT) | 显著不同 |
| `expr.cc/hh` | ✅ 使用 | ✅ 大幅扩展(等价变换框架) | EET 核心改动 |
| `dut.cc/hh` | ✅ 扩展(mysql/mariadb/tidb) | ✅ 扩展(mysql/pg/sqlite/clickhouse/tidb/ob/yugabyte/cockroach) | 各自扩展 |
| `impedance.cc/hh` | ✅ 使用 | ✅ 使用 | 基本未改 |

### 3.2 TxCheck 独有组件

| 文件 | 功能 | 行数(~) |
|------|------|---------|
| `transaction_test.cc/hh` | 事务测试全流程（生成→插桩→执行→去环→oracle） | 1300 |
| `instrumentor.cc/hh` | SQL级插桩（BWR/AWR/VSR） | 250 |
| `dependency_analyzer.cc/hh` | 依赖图构建、环检测、拓扑排序 | 1800 |
| `dbms_info.cc/hh` | DBMS配置管理 | 100 |

### 3.3 EET 独有组件

| 文件 | 功能 | 行数(~) |
|------|------|---------|
| `qcn.cc` | 主入口 + 测试循环 + DB最小化 | 450 |
| `qcn_tester/*.cc/hh` | 测试框架（5种tester: select/update/delete/cte/insert-select） | 800 |
| `value_expr/value_expr.hh` | 表达式基类 + 变换框架 + component_id | 70 |
| `value_expr/bool_expr/*.cc/hh` | 布尔表达式变换 | 150 |
| `value_expr/case_expr.cc/hh` | CASE表达式变换 | 100 |
| `unioned_query.cc/hh` | UNION/INTERSECT/EXCEPT变换 | 200 |

### 3.4 架构差异的本质

**TxCheck** 的架构是**流水线型**：一条测试用例经过多个阶段逐步精化（插桩→阻塞调度→执行→依赖分析→去环→oracle检查），每个阶段都可能修改测试用例。

**EET** 的架构是**对照型**：生成一条原始查询，对其做等价变换得到变体查询，两者在同一数据库上分别执行，比较结果。变换是纯 AST 操作，不需要修改数据库或做多次执行。

---

## 4. DBMS 支持对比

| DBMS | TxCheck | EET | 备注 |
|------|---------|-----|------|
| MySQL | ✅ (8.0.x) | ✅ (8.0.34) | TxCheck 使用非阻塞API |
| MariaDB | ✅ (10.x) | ❌ | TxCheck 使用特殊的非阻塞查询API |
| PostgreSQL | ❌ | ✅ (多版本) | TxCheck 论文未测试 PG |
| SQLite | ❌ | ✅ | 无事务并发需求 |
| ClickHouse | ❌ | ✅ | 分布式引擎 |
| TiDB | ✅ (5.x SI) | ✅ (多版本) | TxCheck 仅测乐观+SI模式 |
| OceanBase | ❌ | ✅ | — |
| Yugabyte | ❌ | ✅ | — |
| CockroachDB | ❌ | ✅ | — |

**关键差异**：TxCheck 支持的 DBMS 更少但更深入（每个 DBMS ~650 行代码，含阻塞检测）；EET 支持更多 DBMS 但接口更简单（每个 ~200 行，无阻塞需求）。

---

## 5. Bug 检测能力对比

### 5.1 已发现 bug 统计

| DBMS | TxCheck 确认 bug | EET 确认 bug | 重叠？ |
|------|-------------------|---------------|--------|
| TiDB | 19 | 10 | 需交叉验证 |
| MySQL | 18 | 16 | 需交叉验证 |
| MariaDB | 15 | 0 | EET 不测 MariaDB |
| PostgreSQL | 0 | 9 | TxCheck 不测 PG |
| SQLite | 0 | 10 | TxCheck 不测 SQLite |
| ClickHouse | 0 | 21 | TxCheck 不测 CH |
| **总计** | **52** | **66** | **大概率不重叠** |

### 5.2 bug 类型覆盖

| bug 类型 | TxCheck 可检测 | EET 可检测 |
|----------|---------------|------------|
| 隔离级别违反 (G1a/G1b/G1c) | ✅ | ❌ |
| 中止事务副作用 | ✅ | ❌ |
| 事务与非事务结果不一致 | ✅ | ❌ |
| 查询优化器错误选择 | ❌ | ✅ |
| 谓词评估错误 | ❌（仅事务上下文） | ✅（单语句上下文） |
| 表达式计算错误 | ❌ | ✅ |
| CASE 表达式 bug | ❌ | ✅ |
| 三值逻辑 (NULL) 处理错误 | ❌ | ✅ |
| Crash / 内存 bug | ✅（附带） | ✅（附带） |

**结论**：二者的 bug 空间几乎正交（不重叠），互补价值极高。

---

## 6. 集成可行性分析

### 6.1 可集成的基础

两个项目共享 SQLsmith 的核心模块（relmodel, prod, random, schema, grammar 的基础部分, dut, impedance），有共同的作者，且 C++ 语言 + Autotools 构建系统一致。

### 6.2 集成的技术难点

| 难点 | 详情 | 严重程度 |
|------|------|----------|
| **grammar.cc 分叉** | TxCheck 扩展了事务语句（BEGIN/COMMIT/ABORT）+ pkey/wkey 约束；EET 扩展了 CTE + UNION/INTERSECT/EXCEPT + INSERT-SELECT + 等价变换。两个 grammar.cc 有显著差异 | ⚠️ 高 — 需合并两套语法扩展 |
| **expr.cc / value_expr 分叉** | EET 的 `value_expr` 增加了 `equivalent_transform()`, `back_transform()`, `component_id`, `is_transformed` 等；TxCheck 的 expr 未改动。需要将 EET 的变换框架移植到 TxCheck 的 expr 中 | ⚠️ 中 — EET 的变换框架是自包含的，可以叠加 |
| **Schema 约束冲突** | TxCheck 要求每张表有 `pkey` + `wkey` 列；EET 无此约束。TxCheck 的 UPDATE/INSERT 语句必须设置 wkey 为新值 | ⚠️ 中 — 需在数据库生成阶段统一处理 |
| **DBMS 支持差异** | TxCheck 的 dut_mysql 使用非阻塞 API (`mysql_real_query_nonblocking`)；EET 使用标准 API。两者 DUT 实现不同 | ⚠️ 低 — 可以共存，TxCheck 模式用非阻塞，EET 模式用标准 |
| **测试循环架构差异** | TxCheck 是多轮迭代精化流水线；EET 是一次性对照。主循环结构完全不同 | ⚠️ 高 — 需重新设计统一的测试循环 |
| **C++ 标准差异** | TxCheck 用 C++11；EET 用 C++17 | ⚠️ 低 — 统一到 C++17 即可 |

### 6.3 集成模式建议

我建议三种集成模式，按推荐优先级排序：

---

### 🏆 方案 A：共底座 + 双模式运行（推荐）

**思路**：合并两个项目的共同基础代码（SQLsmith 核心 + 语法扩展），构建一个统一的二进制，通过命令行参数选择 TxCheck 模式或 EET 模式。

**架构设计**：

```
                    ┌──────────────────────┐
                    │  统一 SQLsmith 核心    │
                    │  (relmodel, prod,     │
                    │   random, impedance)  │
                    └────────────┬─────────┘
                                 │
                    ┌────────────┴─────────┐
                    │   统一 grammar.cc      │
                    │  (合并两套语法扩展)     │
                    │  + 统一 schema.cc       │
                    │  + 统一 dut.cc/hh       │
                    └────────────┬─────────┘
                                 │
               ┌─────────────────┴──────────────────┐
               │                                    │
    ┌──────────┴──────────┐          ┌──────────────┴──────────────┐
    │  TxCheck 模式        │          │  EET 模式                    │
    │  (--mode=txcheck)    │          │  (--mode=eet)               │
    │                      │          │                              │
    │  • transaction_test  │          │  • qcn_tester               │
    │  • instrumentor      │          │  • equivalent_transform     │
    │  • dependency_analyzer│          │  • component minimization   │
    │  • block_scheduling  │          │                              │
    │  • oracle comparison │          │  • oracle comparison        │
    │    (事务 vs 非事务)   │          │    (原始 vs 变换)           │
    └──────────────────────┘          └─────────────────────────────┘
```

**关键实现步骤**：

1. **合并 grammar.cc**：将 TxCheck 的事务语句（BEGIN/COMMIT/ABORT/txn_string_stmt）和 pkey/wkey 约束，与 EET 的 CTE/UNION/INTERSECT/EXCEPT/INSERT-SELECT/等价变换框架合并到一个 grammar.cc 中。需要处理：
   - TxCheck 的 `update_stmt` 必须设置 wkey（EET 不需要），在 TxCheck 模式下启用此约束
   - EET 的 `common_table_expression` 和 `unioned_query` 在 TxCheck 模式下也可用（事务内可用 CTE）
   - 用条件编译或运行时模式开关控制约束

2. **合并 value_expr / expr**：将 EET 的 `equivalent_transform()` 和 `back_transform()` 框架移植到 TxCheck 的表达式中。EET 的变换框架是纯 AST 操作，不依赖任何测试模式，可以直接叠加。

3. **统一数据库生成**：
   - TxCheck 模式：生成的表必须有 `pkey` + `wkey`
   - EET 模式：无此约束
   - 统一生成器根据模式动态添加/跳过 pkey/wkey 列

4. **统一 schema 提取和 DUT**：合并两个项目的 DBMS 支持。优先支持 MySQL + PostgreSQL + TiDB + SQLite + ClickHouse。

5. **统一主循环**：
```cpp
int main(int argc, char* argv[]) {
    // 解析 --mode=txcheck 或 --mode=eet
    if (mode == "txcheck") {
        // TxCheck 测试循环
        while (1) {
            generate_database(d_info); // 含 pkey/wkey
            transaction_test t(d_info);
            t.test(); // 含插桩+依赖分析+oracle
        }
    } else if (mode == "eet") {
        // EET 测试循环
        while (1) {
            generate_database(d_info); // 无 pkey/wkey
            auto schema = get_schema(d_info);
            for (int i = 0; i < db_test_num; i++) {
                qcn_tester->qcn_test(); // 含变换+比较
            }
        }
    }
}
```

6. **统一测试用例存储**：两种模式的 bug 输出都存到 `found_bugs/`，但子目录区分类型：
   - `found_bugs/bug_N_txcheck/` — 事务 bug
   - `found_bugs/bug_N_eet/` — 逻辑 bug

**优势**：
- 最大化代码复用，减少维护成本
- 用户可一键切换测试维度
- 未来可交叉利用（如 TxCheck 模式生成的语句也做 EET 变换）

**劣势**：
- grammar.cc 合合工作量较大
- 两种模式对 schema 的约束不同，需要运行时适配

---

### 方案 B：共享库 + 独立前端

**思路**：将 SQLsmith 核心（relmodel, prod, random, impedance, schema基类, dut基类）抽取为共享库 `libsqlsmith.so`，两个项目各自链接此库，保持独立的二进制。

**架构设计**：

```
┌─────────────────────┐
│  libsqlsmith.so      │
│  (relmodel, prod,    │
│   random, impedance, │
│   schema_base,       │
│   dut_base)          │
└───────┬─────┬───────┘
        │     │
   ┌────┘     └────┐
   │                 │
┌──┴──────┐   ┌─────┴──────┐
│ transfuzz│   │    eet      │
│ (TxCheck)│   │  (EET)     │
└──────────┘   └───────────┘
```

**优势**：
- 改动最小，两个项目保持独立演进
- 各自的 grammar.cc 不需合并
- 共享库的 bug 修复同时受益两个项目

**劣势**：
- 两个独立二进制，用户需分别运行
- grammar.cc 和 schema.cc 的重复维护
- DBMS 支持代码重复（dut_mysql 等）

---

### 方案 C：独立共存 + 交叉触发（最轻量）

**思路**：两个项目完全独立，但在 TxCheck 的测试循环中增加一个轻量交叉步骤：对 TxCheck 发现的正常 bug（非事务 bug），尝试用 EET 变换进一步验证。

**实现**：
```cpp
// 在 TxCheck 的 test() 中，检测到 crash bug 时
if (triggered_normal_bug) {
    // 尝试对触发 crash 的语句做 EET 变换
    // 如果变换后的语句也 crash → 可能是优化器 bug
    // 如果变换后的语句不 crash → 更精确定位了 bug 位置
}
```

**优势**：
- 零改动风险
- 交叉触发提供额外信息
- 各项目保持独立

**劣势**：
- 不是真正的集成，只是交叉利用
- 无法在事务上下文中使用 EET 变换

---

## 7. 最终建议

### 7.1 推荐：方案 A（共底座 + 双模式）

**理由**：

1. **bug 空间正交**：TxCheck 和 EET 检测的 bug 类型几乎不重叠，集成后覆盖面从单维度变为双维度，价值倍增
2. **同一作者**：两个项目同一作者，代码风格一致，合并难度低
3. **共享基础设施占比大**：SQLsmith 核心代码（relmodel + prod + random + impedance + schema基类 + dut基类）占两个项目总代码量的 ~40%，这部分完全相同
4. **语法扩展可合并**：TxCheck 的 CTE 支持和 EET 的 CTE 支持可以合并为更丰富的统一语法；EET 的等价变换框架可以叠加到 TxCheck 的表达式中
5. **未来扩展**：统一底座后，可以探索交叉测试策略（如在事务上下文中对每条语句做等价变换，检测事务 + 逻辑双重 bug）

### 7.2 实施路线

**阶段 1**（~2周）：合并基础代码
- 统一 C++17 标准
- 合合 relmodel/prod/random/impedance（基本无冲突）
- 合合 schema基类 + dut基类
- 合合所有 DBMS 支持（mysql, pg, sqlite, clickhouse, tidb, mariadb, oceanbase, yugabyte, cockroach）

**阶段 2**（~3周）：合并 grammar + expr
- 合合 grammar.cc：TxCheck 事务语句 + EET CTE/UNION/变换
- 合合 expr/value_expr：EET 的 `equivalent_transform()` 框架叠加到 TxCheck 的表达式中
- 处理 pkey/wkey 约束：TxCheck 模式下启用，EET 模式下跳过

**阶段 3**（~1周）：统一主循环 + 命令行接口
- `--mode=txcheck` / `--mode=eet` 参数
- 统一数据库生成和 backup/restore
- 统一 bug 报告格式

**阶段 4**（探索性）：交叉测试策略
- TxCheck 模式下，对事务中的每条语句也做 EET 变换（检测"事务中的逻辑 bug"）
- EET 模式下，对 UPDATE/DELETE 的 WHERE 子句做等价变换后比较受影响行数

### 7.3 需要注意的风险

1. **grammar.cc 是最复杂的合并点**：两个项目的 grammar.cc 都有大量修改（TxCheck ~500行新增，EET ~1000行新增），合合时需要仔细处理语句类型的概率分配
2. **pkey/wkey 约束**：EET 生成的 UPDATE/INSERT 不设置 wkey，TxCheck 模式下需要自动添加 wkey 赋值
3. **测试用例最小化差异**：TxCheck 用两级 delta debugging（事务级+语句级），EET 用 component-based minimization + database minimization，需要保留两种策略
4. **性能**：TxCheck 每轮测试更重（插桩+多事务+阻塞调度+重置数据库），EET 每轮更轻（单语句+变换+比较）。统一工具需要在两种模式下保持各自的原有性能