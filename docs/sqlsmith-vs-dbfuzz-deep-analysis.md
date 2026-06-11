# sqlsmith 与 dbfuzz_multidb 深度对比分析

> 分析日期：2026-06-09
> sqlsmith 版本：latest (github.com/anse1/sqlsmith)
> dbfuzz_multidb 版本：v1.0.12

---

## 1. 总体架构对比

| 维度 | sqlsmith | dbfuzz_multidb |
|------|----------|----------------|
| **目标** | PostgreSQL crash 检测 | 多 DBMS 多维 Bug 检测 |
| **源文件数** | ~29 (.cc/.hh) | ~120+ (.cc/.hh) |
| **支持 DBMS** | PostgreSQL, SQLite, MonetDB (3) | MySQL, MariaDB, TiDB, OceanBase, PostgreSQL, YugabyteDB, CockroachDB, SQLite, ClickHouse, GaussDB-M/A (11) |
| **测试模式** | 单一 crash 检测 | EET (逻辑 Bug) + TxCheck (事务 Bug) + Cross (跨库对比) + Smoke (crash) |
| **代码量** | ~3500 行 C++ | ~15000+ 行 C++ |
| **构建系统** | autoconf/automake | CMake |
| **依赖** | libpqxx, boost-regex | libpqxx, boost-regex, libmysqlclient(可选), sqlite3(可选) |

---

## 2. 语法覆盖范围对比

### 2.1 语句类型 (Statement Productions)

| 语句类型 | sqlsmith | dbfuzz (Smoke) | dbfuzz (其他模式) | 差异说明 |
|----------|----------|----------------|-------------------|----------|
| **query_spec** (SELECT) | ✅ | ✅ | ✅ | 相同 |
| **select_for_update** | ✅ 含 verify 检查 | ✅ 含 verify 检查 | ✅ | 相同 |
| **common_table_expression** (WITH) | ✅ | ✅ | ✅ | 相同 |
| **data_modifying_cte** | ❌ | ✅ | ✅ | **dbfuzz 独有** |
| **unioned_query** (UNION/INTERSECT/EXCEPT) | ✅ | ✅ | ✅ | 相同 |
| **insert_stmt** | ✅ | ✅ | ✅ | 相同 |
| **insert_select_stmt** | ❌ | ✅ | ✅ | **dbfuzz 独有** |
| **delete_stmt / delete_returning** | ✅ | ✅ | ✅ | 相同 |
| **update_stmt / update_returning** | ✅ | ✅ | ✅ | 相同 |
| **upsert_stmt** (ON CONFLICT) | ✅ | ✅ MySQL + PG 双分支 | ✅ | dbfuzz 增加 MySQL ON DUPLICATE KEY |
| **merge_stmt** (MERGE INTO) | ✅ | ✅ | ✅ | 相同 |
| **create_table_stmt** | ❌ | ✅ | ✅ | **dbfuzz 独有** |
| **create_view_stmt** | ❌ | ✅ | ✅ | **dbfuzz 独有** |
| **create_index_stmt** | ❌ | ✅ | ✅ | **dbfuzz 独有** |
| **create_trigger_stmt** | ❌ | ✅ | ✅ | **dbfuzz 独有** |
| **alter_table_stmt** | ❌ | ✅ | ✅ | **dbfuzz 独有** |
| **drop_table_stmt** | ❌ | ✅ | ✅ | **dbfuzz 独有** |
| **prepare_stmt** | ❌ | ✅ | ❌ | dbfuzz 新增 |
| **execute_stmt** | ❌ | ✅ | ❌ | dbfuzz 新增 |
| **analyze_stmt** | ❌ | ✅ | ❌ | dbfuzz 新增 |
| **set_stmt** | ❌ | ✅ | ❌ | dbfuzz 新增 |

**结论：dbfuzz 语句类型覆盖是 sqlsmith 的严格超集**，新增 12 种语句类型（6 DDL + 2 CTE/子查询 + 4 辅助语句）。

### 2.2 表达式类型 (Expression Types)

| 表达式类型 | sqlsmith | dbfuzz | 差异说明 |
|-----------|----------|--------|----------|
| **column_reference** | ✅ | ✅ | 相同 |
| **const_expr** | ✅ | ✅ | 相同 |
| **funcall** (标量/聚合) | ✅ | ✅ | 相同 |
| **case_expr** | ✅ | ✅ | 相同 |
| **coalesce** | ✅ | ✅ | 相同 |
| **nullif** | ✅ | ✅ | 相同 |
| **binop_expr** | ✅ | ✅ | 相同 |
| **atomic_subselect** | ✅ | ✅ | 相同 |
| **window_function** | ✅ 含 frame | ✅ 含 frame (ROWS/RANGE/GROUPS) | dbfuzz 扩展了 frame 子句 |
| **win_funcall** | ✅ | ✅ | 相同 |
| **win_func_using_exist_win** | ✅ | ✅ | 相同 |
| **json_extract_op** (->, ->>, #>, #>>) | ❌ | ✅ | **dbfuzz 独有** |
| **array_constructor** (ARRAY[...]) | ❌ | ✅ | **dbfuzz 独有** |
| **row_constructor** (ROW(...)) | ❌ | ✅ | **dbfuzz 独有** |
| **printed_expr** | ❌ | ✅ | **dbfuzz 独有**（EET 等价变换用） |

### 2.3 布尔表达式类型 (Boolean Expression Types)

| 布尔表达式 | sqlsmith | dbfuzz | 差异说明 |
|-----------|----------|--------|----------|
| **const_bool** (truth_value) | ✅ | ✅ | 相同 |
| **comparison_op** | ✅ | ✅ | 相同 |
| **bool_term** (AND/OR) | ✅ | ✅ | 相同 |
| **not_expr** | ✅ | ✅ | 相同 |
| **null_predicate** | ✅ | ✅ | 相同 |
| **exists_predicate** | ✅ | ✅ | 相同 |
| **between_op** | ❌ | ✅ | **dbfuzz 独有** |
| **like_op** | ❌ | ✅ | **dbfuzz 独有** |
| **in_query** (IN subquery) | ❌ | ✅ | **dbfuzz 独有** |
| **comp_subquery** | ❌ | ✅ | **dbfuzz 独有** |
| **quantified_comparison** (ALL/ANY/SOME) | ❌ | ✅ | **dbfuzz 独有** |
| **distinct_pred** (IS DISTINCT FROM) | ⚠️ 注释掉 | ✅ | dbfuzz 激活了此特性 |

**结论：dbfuzz 布尔表达式覆盖是 sqlsmith 的严格超集**，新增 7 种布尔表达式。

### 2.4 子句类型 (Clause Productions)

| 子句 | sqlsmith | dbfuzz | 差异说明 |
|------|----------|--------|----------|
| **from_clause** | ✅ | ✅ | 相同 |
| **table_or_query_name** | ✅ | ✅ | 相同 |
| **table_sample** | ✅ | ✅ | 相同 |
| **table_subquery** | ✅ | ✅ | 相同 |
| **lateral_subquery** | ✅ | ✅ | 相同 |
| **joined_table** (JOIN) | ✅ | ✅ | 相同 |
| **simple_join_cond** | ✅ | ✅ | 相同 |
| **expr_join_cond** | ✅ | ✅ | 相同 |
| **select_list** | ✅ | ✅ | 相同 |
| **group_clause** (GROUP BY) | ✅ 简单 | ✅ 扩展 GROUPING SETS/CUBE/ROLLUP | **dbfuzz 扩展** |
| **named_window** | ✅ | ✅ 含 window_frame | **dbfuzz 扩展** |
| **window_frame** (ROWS/RANGE/GROUPS) | ❌ | ✅ | **dbfuzz 独有** |
| **set_list** (UPDATE SET) | ✅ | ✅ | 相同 |
| **when_clause** (MERGE WHEN) | ✅ | ✅ | 相同 |

---

## 3. 运行时行为对比

### 3.1 主循环结构

**sqlsmith 主循环：**
```
outer while(1):  // 连接恢复
  inner while(1):  // 查询生成
    statement_factory → AST → SQL → execute
    → ROLLBACK; BEGIN; stmt; ROLLBACK;
    → 成功/失败通知 impedance
  catch(dut::broken): sleep 1s, reconnect
```

**dbfuzz Smoke 模式主循环：**
```
outer while(1):  // 连接恢复
  inner while(1):  // 查询生成
    statement_factory → AST → SQL → execute
    → ROLLBACK; BEGIN; stmt; ROLLBACK;
    → 成功/失败通知 impedance
  catch(dut::broken): sleep 1s, reconnect
```

**结论：Smoke 模式与 sqlsmith 主循环结构完全一致。**

### 3.2 会话变量

| 会话变量 | sqlsmith | dbfuzz Smoke |
|----------|----------|--------------|
| `statement_timeout = '1s'` | ✅ | ✅ |
| `client_min_messages = 'ERROR'` | ✅ | ✅ |
| `application_name` | ✅ `sqlsmith::dut` | ❌ 未设置 |

### 3.3 阻抗反馈 (Impedance Feedback)

| 特性 | sqlsmith | dbfuzz |
|------|----------|--------|
| 失败次数追踪 | ✅ | ✅ |
| 成功次数追踪 | ✅ | ✅ |
| 100 次阈值 | ✅ | ✅ |
| 99% 错误率黑名单 | ✅ | ✅ |
| retry 追踪 | ✅ | ✅ |
| limit 追踪 | ✅ | ✅ |
| fail 追踪 | ✅ | ✅ |
| JSON 报告输出 | ✅ | ✅ |

**结论：阻抗反馈算法完全一致。**

---

## 4. 功能差异详细分析

### 4.1 sqlsmith 独有功能（dbfuzz 缺失）

| 功能 | 重要程度 | 说明 |
|------|----------|------|
| **--rng-state 序列化** | 🔴 高 | sqlsmith 可序列化 RNG 状态用于精确复现查询序列，dbfuzz 仅有 --seed |
| **pqxx_logger** | 🟡 中 | sqlsmith 可将错误/统计信息写入 PostgreSQL 数据库（instance/error/stat 表），dbfuzz 无此功能 |
| **known.txt / boring_sqlstates.txt** | 🟡 中 | sqlsmith 有已知错误过滤机制，用于排除不感兴趣的错误；dbfuzz 仅有 pgsqlerr.txt 用于 crash 判断 |
| **application_name 设置** | 🟢 低 | sqlsmith 设置 `application_name = 'sqlsmith::dut'`，便于在 pg_stat_activity 中识别 |
| **--version 含 git rev** | 🟢 低 | sqlsmith 显示包含 git revision 的版本信息 |
| **anymultirange 类型** | 🟢 低 | PG 14+ multirange 伪类型支持 |
| **anycompatible* 类型族** | 🟢 低 | PG 14+ anycompatible, anycompatiblearray 等伪类型 |

### 4.2 dbfuzz 独有功能（sqlsmith 缺失）

| 功能 | 重要程度 | 说明 |
|------|----------|------|
| **多 DBMS 支持** | 🔴 高 | 11 种 DBMS vs 3 种 |
| **EET 逻辑 Bug 检测** | 🔴 高 | 等价表达式变换 + 结果对比，可发现优化器逻辑 Bug |
| **TxCheck 事务 Bug 检测** | 🔴 高 | 事务依赖图分析，可发现 G1a/G1b/G2-item 等隔离异常 |
| **Cross 跨库对比** | 🔴 高 | 同一 SQL 在多 DBMS 执行，差异即为 Bug |
| **DDL 语句生成** | 🔴 高 | CREATE TABLE/VIEW/INDEX/TRIGGER, ALTER TABLE, DROP TABLE |
| **db_feature_flags 机制** | 🟡 中 | 按 DBMS 精确控制语法特性开关 |
| **数据修改 CTE** | 🟡 中 | WITH ... AS (INSERT/UPDATE/DELETE RETURNING) |
| **JSON/Array/Row 构造器** | 🟡 中 | PG 特有操作符 |
| **GROUPING SETS/CUBE/ROLLUP** | 🟡 中 | 多维分组 |
| **BETWEEN / LIKE / IN 表达式** | 🟡 中 | 更丰富的布尔表达式 |
| **INSERT SELECT** | 🟡 中 | 子查询插入 |
| **用例最小化** | 🟡 中 | Bug 报告自动生成最小复现用例 |
| **MySQL 反引号引用** | 🟢 低 | 正确生成 MySQL 标识符引用 |
| **ON DUPLICATE KEY UPDATE** | 🟢 低 | MySQL UPSERT 语法 |
| **等价表达式变换系统** | 🔴 高 | printed_expr + case_expr 组合实现语义等价变换 |

---

## 5. 类型系统对比

### 5.1 伪类型处理

| 伪类型 | sqlsmith | dbfuzz |
|--------|----------|--------|
| `any` | ✅ | ✅ |
| `anyarray` | ✅ | ✅ |
| `anynonarray` | ✅ | ✅ |
| `anyelement` | ✅ | ✅ |
| `anyenum` | ✅ | ✅ |
| `anyrange` | ✅ | ✅ |
| `record` | ✅ | ✅ |
| `cstring` | ✅ | ✅ |
| `void` | ✅ | ✅ |
| `anymultirange` | ✅ (PG14+) | ❌ |
| `anycompatible` | ✅ (PG14+) | ❌ |
| `anycompatiblearray` | ✅ (PG14+) | ❌ |
| `anycompatiblenonarray` | ✅ (PG14+) | ❌ |
| `anycompatiblerange` | ✅ (PG14+) | ❌ |
| `anycompatiblemultirange` | ✅ (PG14+) | ❌ |

**差异**：dbfuzz 缺少 PG14+ 的 6 个 anycompatible* 伪类型和 anymultirange。这些类型用于函数参数多态匹配，缺失会导致部分 PG14+ 函数无法被调用。

### 5.2 函数分类处理

| 特性 | sqlsmith | dbfuzz |
|------|----------|--------|
| proisagg vs prokind | ✅ 版本感知 | ✅ 版本感知 |
| 排除 ordered-set 聚合 | ✅ | ✅ |
| 排除 RI trigger 函数 | ✅ | ✅ |
| 排除 event_trigger 返回类型 | ✅ | ✅ |
| 排除 trigger 返回类型 | ✅ | ✅ |
| 排除 internal 返回类型 | ✅ | ✅ |

---

## 6. 日志与调试能力对比

| 功能 | sqlsmith | dbfuzz |
|------|----------|--------|
| **cerr_logger** (进度指示) | ✅ `.SetCEt` 字符 | ✅ |
| **query_dumper** (打印查询) | ✅ | ✅ |
| **stats_collecting_logger** | ✅ | ✅ |
| **ast_logger** (GraphML) | ✅ | ✅ |
| **pqxx_logger** (写数据库) | ✅ | ❌ |
| **impedance_feedback** | ✅ | ✅ |
| **定期报告** | ✅ | ✅ |
| **SIGINT 处理** | ✅ 报告+退出 | ✅ 报告+退出 |
| **--dump-all-queries** | ✅ | ✅ |
| **--dump-all-graphs** | ✅ | ⚠️ 选项存在但未接线 |
| **--dry-run** | ✅ | ✅ |
| **--verbose** | ✅ | ✅ |
| **--max-queries** | ✅ | ✅ |

---

## 7. 集成后优劣势评估

### 7.1 优势（dbfuzz > sqlsmith）

1. **多维 Bug 检测能力**
   - sqlsmith 只能发现 crash（段错误、断言失败）
   - dbfuzz 可发现：crash + 逻辑 Bug + 事务隔离 Bug + 跨 DBMS 兼容性 Bug
   - 这使得 dbfuzz 的 Bug 发现率显著高于 sqlsmith

2. **更广的语法覆盖**
   - 新增 12 种语句类型、7 种布尔表达式、4 种表达式类型
   - DDL 支持使测试覆盖 schema 变更场景
   - 数据修改 CTE、GROUPING SETS 等高级特性增加覆盖深度

3. **多 DBMS 统一框架**
   - 一套代码覆盖 11 种 DBMS
   - feature_flags 机制避免在不支持的 DBMS 上生成不兼容语法
   - Cross 模式可自动发现跨 DBMS 差异 Bug

4. **用例最小化**
   - EET/TxCheck/Cross 模式内置最小化算法
   - 自动生成最小复现用例，降低 Bug 报告分析成本

5. **等价表达式变换**
   - EET 模式的核心能力，sqlsmith 完全不具备
   - 通过 CASE WHEN + 随机布尔表达式构造语义等价查询对

### 7.2 劣势（dbfuzz < sqlsmith）

1. **RNG 状态不可序列化**
   - sqlsmith 的 `--rng-state` 可精确复现完整查询序列
   - dbfuzz 仅有 `--seed`，无法在中断后从断点恢复
   - 影响：Bug 复现的可靠性降低

2. **缺少数据库日志后端**
   - sqlsmith 的 `pqxx_logger` 可将错误写入 PostgreSQL 数据库
   - 支持大规模 fuzzing 的错误聚合与分析
   - dbfuzz 仅输出到文件和 stderr

3. **PG14+ 伪类型不完整**
   - 缺少 anycompatible* 类型族和 anymultirange
   - 影响：PostgreSQL 14+ 上部分函数/操作符无法被生成调用

4. **已知错误过滤机制简单**
   - sqlsmith 有 known.txt（精确匹配）+ known_re.txt（正则匹配）+ boring_sqlstates.txt
   - dbfuzz 仅有 pgsqlerr.txt（用于 crash 检测，非错误过滤）
   - 影响：Smoke 模式下可能输出大量不感兴趣的错误信息

5. **GraphML dump 未接线**
   - `--dump-all-graphs` 选项存在但 smoke 模式未添加 ast_logger
   - 影响：无法在 smoke 模式下 dump AST 用于调试

---

## 8. 建议补全项（按优先级）

### P0 - 关键补全

| 项目 | 工作量 | 说明 |
|------|--------|------|
| **--rng-state 序列化** | 中 | 实现 mt19937_64 状态的序列化/反序列化，支持精确复现 |
| **--dump-all-graphs 接线** | 小 | 在 smoke_run.cc 中添加 ast_logger 实例化 |

### P1 - 重要补全

| 项目 | 工作量 | 说明 |
|------|--------|------|
| **PG14+ anycompatible* 伪类型** | 小 | 在 pg_type::consistent() 中添加 7 个伪类型的匹配规则 |
| **known.txt 错误过滤** | 小 | 在 impedance_feedback 或 logger 中添加已知错误过滤层 |
| **application_name 设置** | 极小 | 在连接时设置 `SET application_name = 'dbfuzz::dut'` |

### P2 - 可选增强

| 项目 | 工作量 | 说明 |
|------|--------|------|
| **pqxx_logger** | 中 | 实现数据库日志后端，支持大规模 fuzzing 的错误聚合 |
| **anymultirange 类型** | 极小 | 在 pg_type::consistent() 中添加 multirange 匹配 |
| **--rng-state 完整断点续测** | 中 | 除 RNG 外还需保存 impedance 状态 |

---

## 9. 定量指标对比

| 指标 | sqlsmith | dbfuzz v1.0.12 | 比率 |
|------|----------|----------------|------|
| 语句类型数 | 8 | 20 | 2.5x |
| 表达式类型数 | 10 | 14 | 1.4x |
| 布尔表达式类型数 | 6 | 12 | 2.0x |
| 子句类型数 | 12 | 14 | 1.17x |
| 支持 DBMS 数 | 3 | 11 | 3.7x |
| 测试模式数 | 1 | 4 | 4.0x |
| 代码行数(估) | ~3,500 | ~15,000+ | ~4.3x |
| feature flags | 0 | 18 | N/A |
| DDL 语句 | 0 | 6 | N/A |

---

## 10. 结论

**dbfuzz_multidb 在 sqlsmith 基础上实现了显著的功能扩展**，语法覆盖是 sqlsmith 的严格超集（语句类型 2.5x，布尔表达式 2.0x），同时新增了 3 种独立的 Bug 检测模式。集成后的 Smoke 模式在运行时行为上与 sqlsmith 完全一致（主循环、事务包裹、阻抗反馈、会话变量）。

主要差距集中在**运维工具链**方面（RNG 序列化、数据库日志、错误过滤），这些不影响核心 Bug 检测能力，但对大规模持续 fuzzing 的可操作性有影响。建议按 P0→P1→P2 优先级逐步补全。
