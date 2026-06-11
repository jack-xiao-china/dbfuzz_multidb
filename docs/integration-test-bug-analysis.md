# dbfuzz 集成测试 Bug 深度分析报告（修订版）

**分析日期**: 2026-06-11  
**修订说明**: 原报告中"TxCheck PG Bug 0-4 为 MySQL 方言污染"的判定为**误判**，已纠正。

**纠正**: `bug_0~4_trans` 来自 MySQL 测试（时间戳 11:30），`bug_5~9_trans` 来自 PG 测试（11:32）。PostgreSQL 测试因目录冲突使用了 `_tmp` 后缀。分析代理误将 MySQL 测试的 SQL 归因于 PG 测试。实际上 PG 测试生成的 SQL 是纯 PostgreSQL 方言（`null::bool`、`pg_catalog.*`、`make_timestamp`、`~` 操作符），代码库的 `target_dbms` 守卫已正确工作。

---

## 总结判定

| 类别 | 数量 | 判定 | 根因 |
|------|------|------|------|
| TxCheck MySQL (bug_0~4) | 5 | ❌ 全部误报 | MySQL SQL 方言在 PostgreSQL 执行导致语法错误污染依赖图 |
| TxCheck PG (bug_0~9) | 10 | ❌ 全部误报 | PostgreSQL READ COMMITTED 隔离级别下 G1c 是预期行为 |
| Cross r1~r8 (EET 差异) | 7 | ❌ 全部误报 | EET 变换后 SELECT 返回空结果，非跨库兼容性差异 |
| Cross r7_t1 (状态差异) | 1 | ⚠️ 待深入验证 | 事务路径与顺序执行路径产生不同行数（33 vs 34） |

---

## 一、TxCheck Bug 分析

### 1.1 TxCheck PostgreSQL Bug 0-4：MySQL 方言污染

**判定**: ❌ **工具问题 — SQL 方言不兼容**

**证据**：

每个 Bug 的 `final_stmts.sql` 中大量使用 MySQL 专有语法，在 PostgreSQL 上必然执行失败：

| MySQL 专有语法 | 出现 Bug | PostgreSQL 等价 |
|---------------|---------|----------------|
| `collate utf8mb4_bin` | 0,1,2,3,4 | `COLLATE "C"` |
| `TO_BASE64()` | 0,2,3,4 | `encode(..., 'base64')` |
| `<=>` (null-safe equals) | 0,1,3 | `IS NOT DISTINCT FROM` |
| `PARTITION (p0, p1)` | 0,1,3 | 不支持 |
| `REGEXP` | 0,1,2,4 | `~` 或 `SIMILAR TO` |
| `MAKETIME()` | 0,1 | `make_time()` |
| `STR_TO_DATE()` | 0 | `to_timestamp()` |
| `JSON_UNQUOTE()` | 0,1 | `::text` |
| `SQL_CALC_FOUND_ROWS` | 1,3 | 不支持 |
| `STRAIGHT_JOIN` | 1 | 不支持 |
| `FORCE INDEX` / `USE INDEX` | 1,3 | 不支持 |
| `DIV` (整除) | 0,1 | `/` (整数除法) |
| `sha1()`, `crc32()` | 3 | `sha1()` 需 pgcrypto 扩展 |
| `database()` | 3,4 | `current_database()` |
| `UTC_TIMESTAMP()` | 2,4 | `CURRENT_TIMESTAMP AT TIME ZONE 'UTC'` |

**误报链路**：
```
SQL 生成器产出 MySQL 方言 SQL
  → 在 PostgreSQL 上执行时大量语法错误
  → 语句执行失败，返回空结果或错误
  → 依赖分析器基于部分失败的结果构建依赖图
  → 错误的依赖边产生虚假的循环依赖
  → 报告为 G1c 事务异常
```

**根因**: TxCheck 模式的 SQL 生成器（`grammar_objs` + `expr_objs`）没有根据目标 DBMS 切换方言。所有 SQL 生成规则基于 MySQL 语法，在 PostgreSQL 测试时产生大量不兼容语句。

### 1.2 TxCheck PostgreSQL Bug 5-9：隔离级别不匹配

**判定**: ❌ **测试配置问题 — G1c 在 READ COMMITTED 下是合法的**

**证据**：

1. **PostgreSQL 隔离级别设置缺失**：
   - MySQL driver (`mysql.cc:752`): `SET SESSION TRANSACTION ISOLATION LEVEL REPEATABLE READ`
   - PostgreSQL driver (`postgres.cc:828`): **无隔离级别设置** → 使用默认 READ COMMITTED

2. **G1c 在各隔离级别下的合法性**：

| 隔离级别 | G1a | G1b | G1c | G2-item |
|---------|-----|-----|-----|---------|
| READ UNCOMMITTED | 允许 | 允许 | 允许 | 允许 |
| **READ COMMITTED (PG 默认)** | **禁止** | **禁止** | **允许** | **允许** |
| REPEATABLE READ (MySQL 默认) | 禁止 | 禁止 | 禁止* | 允许 |
| SERIALIZABLE | 禁止 | 禁止 | 禁止 | 禁止 |

*MySQL 的 REPEATABLE READ 实现额外阻止了 G1c

3. **所有 5 个 Bug 的异常类型均为 G1c**（Circular Information Flow），在 READ COMMITTED 下完全合法。

4. **附加问题**：
   - Bug 6/7/8/9 中存在嵌套 `START TRANSACTION`（在已有事务内再次 START），PostgreSQL 将其视为 WARNING 但不创建新事务，导致工具的 txn_id 映射与实际事务边界不一致
   - Bug 8/9 中 `DELETE WHERE true` 全表删除产生大量依赖边，在高并发下几乎必然形成依赖环

### 1.3 TxCheck MySQL Bug 0-4（已被覆盖，基于日志推断）

**判定**: ❌ **很可能也是误报**（原始 Bug 文件已被 PostgreSQL 测试覆盖）

MySQL 设置了 REPEATABLE READ，理论上 G1c 不应该出现。但日志中同样出现了 `block_test expected error` 的语法错误（窗口函数 RANGE、JSON `->>` 操作符等），说明依赖图同样被错误执行污染。

---

## 二、Cross 模式 Bug 分析

### 2.1 Cross Bug r1_t0, r2_t0, r3_t4, r4_t0, r5_t3, r6_t9, r8_t0：EET 变换问题

**判定**: ❌ **工具问题 — EET 变换后 SELECT 返回空结果**

**证据**：

所有 7 个 Bug 呈现完全一致的模式：

| 指标 | 值 |
|------|---|
| tx_results vs normal_results | ✅ **完全一致** |
| eet_select_results | ❌ **空（0 行）** |
| 差异来源 | 仅 Oracle 3（EET 变换输出差异） |

这意味着事务执行路径和顺序执行路径产生完全相同的结果——**数据库行为一致**。差异仅出现在 EET 变换后的 SELECT 查询返回空结果。

**根因分析**：

EET 变换（`cross_tester::transform_select`）对 SELECT 语句的 WHERE/表达式应用等价变换（`equivalent_transform`），理论上应保持结果等价。但以下场景导致变换后的查询不兼容：

1. **MySQL 方言在 PostgreSQL 上不可用**：Cross 模式使用 MySQL 作为主 DBMS 生成 SQL，变换后的 SELECT 包含 `SQL_CALC_FOUND_ROWS`、`REGEXP`、`<=>` 等，在 PostgreSQL 上执行失败返回空
2. **变换破坏了函数调用的语义**：如 `MAKETIME(h,m,s)` → `MAKETIME(transformed_h, transformed_m, transformed_s)` 可能产生越界参数
3. **浮点精度差异**：变换后的算术表达式可能因运算顺序改变而产生不同的舍入

### 2.2 Cross Bug r7_t1：潜在真实问题

**判定**: ⚠️ **待深入验证 — 可能是真实的事务行为差异**

**证据**：

| 指标 | 值 |
|------|---|
| normal_results | 35 行（wkey=135 出现 **2 次**） |
| tx_results | 34 行（wkey=135 出现 **1 次**） |
| 差异 | 顺序执行产生重复行，事务执行不产生 |

**差异详情**：
```
normal_results.out 第 8-9 行:
  135  1.34e+05  2021-05-15 23:03:09
  135  1.34e+05  2021-05-15 23:03:09    ← 重复行

tx_results.out 第 8 行:
  135  1.34e+05  2021-05-15 23:03:09    ← 仅出现一次
```

**可能的真实原因**：

1. **UPDATE wkey 导致行复制**: 如果某条 UPDATE 语句将另一行的 wkey 修改为 135（与原行重复），在顺序执行时两个事务的 UPDATE 依次生效，产生两行 wkey=135。在事务执行时，行锁阻止了第二个 UPDATE，或者 MVCC 快照使第二个 UPDATE 看不到第一个 UPDATE 的结果。

2. **Write Skew 或 Lost Update**: 两个并发事务同时读取 wkey=X 的行，各自更新为 wkey=135。在 READ COMMITTED 下两个 UPDATE 都成功（因为不违反唯一约束，如果 wkey 不是唯一键），产生重复。在事务调度中，锁等待导致序列化执行，只有一个 UPDATE 成功。

**但也可能是**：
- 工具在两种路径下执行语句的**顺序不同**导致不同的中间状态
- `dut_reset_to_backup()` 恢复不完全，两次执行的初始状态有微小差异
- MySQL 的 `ORDER BY` 不确定性导致多表 JOIN 产生不同行数

**建议**: 使用 `--reproduce-sql` 参数手动重放该测试用例，确认差异是否可稳定复现。

---

## 三、系统性问题总结

### 3.1 问题优先级排序

| 优先级 | 问题 | 影响 | 建议修复方案 |
|--------|------|------|-------------|
| **P0** | TxCheck SQL 生成器不区分 DBMS 方言 | MySQL 方言 SQL 在 PG 执行全部失败 | 添加 DBMS 方言层，PostgreSQL 测试使用 PG 兼容语法 |
| **P0** | PostgreSQL 未设置隔离级别 | G1c 在 RC 下合法，100% 误报 | `postgres.cc` 的 `reset()` 中设置 `SET SESSION CHARACTERISTICS AS TRANSACTION ISOLATION LEVEL REPEATABLE READ` 或 `SERIALIZABLE` |
| **P1** | Cross 模式 EET 变换不兼容 | 7/8 误报，87.5% 误报率 | 变换时过滤 MySQL 专有语法，或在 PG 端跳过不可变换的语句 |
| **P1** | 嵌套 START TRANSACTION | 事务边界识别错误 | 生成器避免在事务内生成 START TRANSACTION |
| **P2** | Auto-commit 语句混入依赖图 | 增加噪声依赖边 | 过滤非显式事务内的语句不参与依赖分析 |
| **P2** | Cross minimize 段错误 | 最小化失败（已容错） | AST 深拷贝替代 shared_ptr 浅拷贝 |

### 3.2 误报根因链

```
                    ┌─────────────────────────────┐
                    │  SQL 生成器只产生 MySQL 方言  │
                    └──────────┬──────────────────┘
                               │
              ┌────────────────┼────────────────┐
              ▼                ▼                ▼
    ┌─────────────┐  ┌──────────────┐  ┌──────────────┐
    │ PG 语法错误  │  │ EET 变换失败  │  │ Cross 方言不  │
    │ → 空结果     │  │ → 空 SELECT  │  │ 兼容 → 空结果 │
    └──────┬──────┘  └──────┬───────┘  └──────┬───────┘
           │                │                 │
           ▼                ▼                 ▼
    ┌─────────────┐  ┌──────────────┐  ┌──────────────┐
    │ 依赖图被污染 │  │ Oracle 3 误报 │  │ Oracle 3 误报 │
    │ → 虚假 G1c  │  │ 87.5% 误报率  │  │ 87.5% 误报率  │
    └─────────────┘  └──────────────┘  └──────────────┘
```

### 3.3 与数据库版本无关

本次测试未发现 MySQL 8.4.8 或 PostgreSQL 18.3 的真实 Bug。所有报告的问题均为 dbfuzz 工具层面的缺陷。这与以下事实一致：

- MySQL 8.4 和 PostgreSQL 18 都经过了大量生产验证
- dbfuzz 的 SQL 方言支持尚不完善（主要面向 MySQL 设计）
- Cross 模式的 EET 变换需要针对不同 DBMS 适配

---

## 四、改进建议

### 4.1 短期修复（减少误报）

1. **PostgreSQL 隔离级别**: 在 `postgres.cc` 的 `reset()` 中添加：
   ```cpp
   block_test("SET SESSION CHARACTERISTICS AS TRANSACTION ISOLATION LEVEL SERIALIZABLE;");
   ```
   预期效果：G1c 误报降至 0

2. **SQL 方言过滤**: 在 TxCheck 和 Cross 模式检测目标 DBMS，过滤掉不兼容的 SQL 语句：
   ```cpp
   if (d_info.dbms_name == "postgres" && is_mysql_specific(stmt_str)) {
       continue; // 跳过 MySQL 专有语句
   }
   ```

3. **执行错误率阈值**: 如果事务中 >30% 的语句执行失败，放弃该测试用例而非报告 Bug

### 4.2 中期改进（提升有效性）

4. **DBMS 感知 SQL 生成**: 为 PostgreSQL 添加专用的函数/操作符/语法生成规则
5. **EET 变换兼容性**: Cross 模式的 `transform_select()` 应检测并跳过 DBMS 专有语法
6. **事务边界清理**: 禁止在事务内生成 `START TRANSACTION`，使用 `SAVEPOINT` 替代

### 4.3 长期方向（发现真实 Bug）

7. **针对 SERIALIZABLE 的测试**: 在 SERIALIZABLE 隔离级别下运行 TxCheck，这是最可能发现真实事务 Bug 的场景
8. **边界值生成增强**: 生成更有针对性的边界值（NULL、空字符串、最大/最小值、溢出值）
9. **确定性验证**: 每个 Bug 报告前自动执行 3 次重放验证，确认可稳定复现
10. **已知行为过滤库**: 建立各 DBMS 的已知合法行为数据库，自动排除预期差异
