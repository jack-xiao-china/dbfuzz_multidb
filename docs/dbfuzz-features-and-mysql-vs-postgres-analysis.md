# dbfuzz 全功能清单与 MySQL vs PostgreSQL 支持差异分析

> 版本：v1.0.14 | 日期：2026-06-09

---

## 一、dbfuzz 全功能清单

### 1. 测试模式（4 种）

| 模式 | 入口 | 目的 |
|------|------|------|
| **TxCheck** | `--mode=txcheck` | 事务并发 Bug 检测（G1a/G1b/G1c/G2-item/G-SIa/G-SIb） |
| **EET** | `--mode=eet` | 逻辑 Bug 检测（QCN 等价变换） |
| **Cross** | `--mode=cross` | 跨数据库对比测试（TxCheck + EET 双路径） |
| **Smoke** | `--mode=smoke` | 高频随机 SQL 生成 + 崩溃检测（sqlsmith 风格） |

### 2. SQL 语句生成能力

#### 2.1 查询语句

| 语句类型 | 类名 | 描述 |
|----------|------|------|
| 基本 SELECT | `query_spec` | SELECT + FROM + WHERE + GROUP BY + HAVING + ORDER BY + LIMIT |
| SELECT FOR UPDATE | `select_for_update` | 行锁查询（UPDATE/SHARE/NO KEY UPDATE/KEY SHARE） |
| UNION/INTERSECT/EXCEPT | `unioned_query` | 集合运算 |
| CTE | `common_table_expression` | WITH 子句（支持递归 CTE） |
| 数据修改 CTE | `data_modifying_cte` | WITH ... AS (DELETE/UPDATE/INSERT RETURNING) SELECT |

#### 2.2 DML 语句

| 语句类型 | 类名 | 描述 |
|----------|------|------|
| INSERT VALUES | `insert_stmt` | 多行 VALUES 插入 |
| INSERT SELECT | `insert_select_stmt` | 子查询插入 |
| UPDATE | `update_stmt` | SET + WHERE + [ORDER BY + LIMIT] |
| DELETE | `delete_stmt` | WHERE + [ORDER BY + LIMIT] |
| DELETE RETURNING | `delete_returning` | DELETE + RETURNING 子句 |
| UPDATE RETURNING | `update_returning` | UPDATE + RETURNING 子句 |
| UPSERT | `upsert_stmt` | ON CONFLICT / ON DUPLICATE KEY UPDATE |
| MERGE | `merge_stmt` | MERGE INTO（PG 15+） |
| REPLACE | `replace_stmt` | MySQL REPLACE INTO |
| DO | `do_stmt` | MySQL DO expr1, expr2... |

#### 2.3 DDL 语句

| 语句类型 | 类名 | 描述 |
|----------|------|------|
| CREATE TABLE | `create_table_stmt` | 主键 + 外键 + CHECK 约束 + 表选项 |
| CREATE TABLE SELECT | `create_table_select_stmt` | CTAS |
| CREATE VIEW | `create_view_stmt` | 视图创建 |
| CREATE INDEX | `create_index_stmt` | 索引创建（UNIQUE/ASC/DESC/部分索引） |
| CREATE TRIGGER | `create_trigger_stmt` | 触发器（BEFORE/AFTER + INSERT/UPDATE/DELETE） |
| ALTER TABLE | `alter_table_stmt` | RENAME/ADD COLUMN |
| DROP TABLE | `drop_table_stmt` | 删除表 |

#### 2.4 辅助语句

| 语句类型 | 类名 | 描述 |
|----------|------|------|
| EXPLAIN | `explain_stmt` | EXPLAIN [ANALYZE] [FORMAT=JSON/TREE/TRADITIONAL] |
| 表维护 | `table_maintenance_stmt` | CHECKSUM/CHECK/OPTIMIZE/REPAIR TABLE |
| SET | `set_stmt` | SET 参数（优化器开关） |
| ANALYZE | `analyze_stmt` | 表/索引统计更新 |
| PREPARE | `prepare_stmt` | 预编译语句 |
| EXECUTE | `execute_stmt` | 执行预编译语句 |

### 3. 表达式系统

#### 3.1 值表达式

| 类型 | 描述 |
|------|------|
| `const_expr` | 字面量（int/real/text/date/bool） |
| `column_reference` | 表.列引用 |
| `atomic_subselect` | 标量子查询 |
| `row_constructor` | ROW() 元组 |
| `binop_expr` | 二元运算（算术/位运算/比较/逻辑） |
| `case_expr` | CASE WHEN...THEN...ELSE END |
| `coalesce` | COALESCE() |
| `funcall` | 标量函数调用 |
| `win_funcall` | 窗口函数调用 |
| `json_expr` | JSON 操作符（PostgreSQL） |

#### 3.2 布尔表达式

| 类型 | 描述 |
|------|------|
| `comparison_op` | =, <>, !=, <, >, <=, >=, <=> |
| `bool_term` | AND, OR |
| `not_expr` | NOT |
| `null_predicate` | IS NULL, IS NOT NULL |
| `distinct_pred` | IS DISTINCT FROM |
| `between_op` | BETWEEN...AND |
| `like_op` | LIKE / NOT LIKE |
| `regexp_expr` | REGEXP / NOT REGEXP / SOUNDS LIKE |
| `in_query` | IN (subquery) |
| `exists_predicate` | EXISTS (subquery) |
| `quantified_comparison` | = ALL/ANY/SOME (subquery) |
| `comp_subquery` | 行值比较 |

#### 3.3 窗口函数 Frame

- 模式：ROWS / RANGE / GROUPS
- 边界：UNBOUNDED PRECEDING / N PRECEDING / CURRENT ROW / N FOLLOWING / UNBOUNDED FOLLOWING

### 4. TxCheck 异常检测（6 种）

| 异常 | 描述 |
|------|------|
| **G1a** | Aborted Reads — 读到已中止事务的写入 |
| **G1b** | Intermediate Reads — 读到非最终版本 |
| **G1c** | Circular Information Flow — 依赖图有向环 |
| **G2-item** | Item Anti-dependency Cycles |
| **G-SIa** | Snapshot Isolation Interference |
| **G-SIb** | Snapshot Isolation Missed Effects |

**依赖类型（9 种）**：WRITE_READ, WRITE_WRITE, READ_WRITE, START_DEPEND, STRICT_START_DEPEND, INSTRUMENT_DEPEND, VERSION_SET_DEPEND, OVERWRITE_DEPEND, INNER_DEPEND

### 5. EET/QCN 等价变换测试（5 种）

| 测试器 | 描述 |
|--------|------|
| `qcn_select_tester` | SELECT 等价变换 |
| `qcn_update_tester` | UPDATE 等价变换 |
| `qcn_delete_tester` | DELETE 等价变换 |
| `qcn_cte_tester` | CTE (WITH) 等价变换 |
| `qcn_insert_select_tester` | INSERT...SELECT 等价变换 |

### 6. 核心机制

| 机制 | 描述 |
|------|------|
| **阻抗反馈** | 自适应语法剪枝（100 次失败 + >99% 错误率 → 黑名单） |
| **已知错误过滤** | 从 known.txt / known_re.txt 加载模式，抑制预期错误 |
| **RNG 状态序列化** | 可恢复/重现的随机状态 |
| **AST 导出** | GraphML 格式输出 |
| **Bug 最小化** | 语句级 + 数据库级 + SELECT 变换级最小化 |
| **Fork-based 执行** | 父子进程 + 超时 + SIGALRM/SIGUSR1 |
| **异步查询** | MySQL 8.x nonblocking API / libpq |
| **锁阻塞检测** | sys.innodb_lock_waits 轮询（MySQL） |

### 7. 支持的 DBMS 目标（11 种）

| DBMS | 客户端库 | 条件编译宏 |
|------|----------|-----------|
| MySQL 8.x | libmysqlclient | `HAVE_MYSQL` |
| MariaDB 10.x | libmysqlclient | `HAVE_MARIADB` |
| TiDB | libmysqlclient | `HAVE_TIDB` |
| OceanBase | libmysqlclient | — |
| PostgreSQL 16 | libpqxx + libpq | 必须 |
| YugabyteDB | libpqxx | — |
| CockroachDB | libpqxx | — |
| SQLite3 | libsqlite3 | `HAVE_LIBSQLITE3` |
| ClickHouse | HTTP API | `TEST_CLICKHOUSE` |
| GaussDB-M (MySQL 兼容) | libmysqlclient | — |
| GaussDB-A (Oracle 兼容) | libpqxx | — |

---

## 二、MySQL vs PostgreSQL 支持差异对比

### 1. 元数据加载方式

| 维度 | MySQL | PostgreSQL |
|------|-------|------------|
| **加载方式** | 硬编码 + INFORMATION_SCHEMA 查询 | 动态从 `pg_catalog` 加载 |
| **操作符来源** | 36 个硬编码 BINOP | `pg_operator` 系统表（运行时发现，通常 50-100+） |
| **函数来源** | ~80 个硬编码 FUNC + 参数签名 | `pg_proc` 系统表（运行时发现，通常 100-200+） |
| **聚合来源** | 31 个硬编码 AGG | `pg_proc` WHERE prokind='a'（运行时发现，通常 30-50+） |
| **窗口函数** | 15 个硬编码 WIN | 动态加载（prokind='w'） |
| **类型系统** | 5 个规范类型 + 20+ 别名映射 | 完整 `pg_type` 加载 + OID 映射 + 14 种伪类型 |

**影响**：PostgreSQL 的函数/操作符覆盖范围随版本自动扩展；MySQL 需要手动添加新函数。

### 2. 类型系统

| 维度 | MySQL | PostgreSQL |
|------|-------|------------|
| **booltype** | tinyint | bool |
| **inttype** | int | int4 |
| **realtype** | double | numeric |
| **texttype** | varchar(200) | text |
| **datetype** | DATETIME | timestamp |
| **internaltype** | internal | internal |
| **arraytype** | ARRAY | anyarray |
| **类型别名** | 20+ 种映射到 5 个规范类型 | 原生 OID 映射，无需别名 |
| **整数别名** | bigint/mediumint/smallint/tinyint/integer → int | — |
| **实数别名** | float/decimal/numeric/real → double | — |
| **文本别名** | varchar/char/text/mediumtext/longtext/tinytext/enum/set → text | — |
| **日期别名** | datetime/timestamp/date/time/year → timestamp | — |
| **布尔别名** | boolean/bool → tinyint | — |
| **伪类型** | ❌ 不支持 | ✅ anyarray, anyelement, anycompatible 等 14 种 |
| **JSON 类型** | ❌ 无原生 JSON 类型 | ✅ json, jsonb |
| **数组类型** | ❌ 不支持 | ✅ 完整数组类型 + 操作符 |

### 3. Feature Flags 对比（26 个 flag）

| Feature Flag | MySQL | PostgreSQL | 说明 |
|-------------|:-----:|:----------:|------|
| `has_window_frame` | ❌ | ✅ | 窗口函数 FRAME 子句 (ROWS/RANGE/GROUPS) |
| `has_data_mod_cte` | ❌ | ✅ | 数据修改 CTE (WITH ... RETURNING) |
| `has_quantified_cmp` | ❌ | ✅ | ALL/ANY/SOME 量化比较 |
| `has_grouping_sets` | ❌ | ✅ | GROUPING SETS/CUBE/ROLLUP |
| `has_json_jsonb` | ❌ | ✅ | JSON/JSONB 操作符 |
| `has_array_ops` | ❌ | ✅ | 数组操作符 |
| `has_merge` | ❌ | ✅ | MERGE 语句 (PG 15+) |
| `has_upsert` | ✅ | ✅ | UPSERT |
| `has_returning` | ❌ | ✅ | RETURNING 子句 |
| `has_tablesample` | ❌ | ✅ | TABLESAMPLE 子句 |
| `has_lateral` | ✅ | ✅ | LATERAL 派生表 |
| `has_for_update` | ✅ | ✅ | FOR UPDATE/SHARE 行锁 |
| `has_on_duplicate_key` | ✅ | ❌ | ON DUPLICATE KEY UPDATE |
| `has_if_function` | ✅ | ❌ | IF() 条件函数 |
| `has_group_concat` | ✅ | ❌ | GROUP_CONCAT 聚合 |
| `has_full_outer_join` | ❌ | ✅ | FULL OUTER JOIN |
| `has_intersect_except` | ❌* | ✅ | INTERSECT/EXCEPT |
| `has_regexp` | ✅ | ❌ | REGEXP/RLIKE |
| `has_sounds_like` | ✅ | ❌ | SOUNDS LIKE |
| `has_straight_join` | ✅ | ❌ | STRAIGHT_JOIN |
| `has_index_hints` | ✅ | ❌ | USE/FORCE/IGNORE INDEX |
| `has_with_rollup` | ✅ | ❌ | GROUP BY ... WITH ROLLUP |
| `has_replace` | ✅ | ❌ | REPLACE 语句 |
| `has_do_stmt` | ✅ | ❌ | DO 语句 |
| `has_explain` | ✅ | ❌ | EXPLAIN 语句 |
| `has_select_options` | ✅ | ❌ | SQL_CALC_FOUND_ROWS 等 |

> \*MySQL 8.0.31+ 支持 INTERSECT/EXCEPT，但 flag 设为 false 以兼容旧版本

**统计**：
- PostgreSQL 启用：**14 个** flag
- MySQL 启用：**15 个** flag
- 两者共同启用：3 个（has_upsert, has_for_update, has_lateral）

### 4. 操作符对比

| 类别 | MySQL（36 个硬编码） | PostgreSQL（动态加载） |
|------|---------------------|----------------------|
| **位运算** | &, >>, <<, ^, \| (5) | 动态 |
| **比较** | >, <, >=, <=, =, <>, !=, <=> (8×3 类型 = 24) | 动态 |
| **算术** | +, -, *, /, %, DIV (6×2 类型 = 12) | 动态 |
| **逻辑** | &&, \|\|, XOR (3) | 动态 |
| **MySQL 独有** | <=> (NULL-safe equal), DIV, XOR | — |
| **PostgreSQL 独有** | — | ~~, !~, @@, <@, @>, &&（数组/JSON）等 |

### 5. 函数/聚合/窗口函数对比

| 类别 | MySQL（硬编码） | PostgreSQL（动态加载） |
|------|----------------|----------------------|
| **数学函数** | 25 个 (ABS, ACOS, ASIN, ATAN, ATAN2, CEILING, COS, COT, CRC32, DEGREES, EXP, FLOOR, LN, LOG, LOG2, LOG10, PI, POW, RADIANS, ROUND, SIGN, SIN, SQRT, TAN, TRUNCATE) | 动态 |
| **日期函数** | 19 个 (ADDDATE, DATEDIFF, DAYOFMONTH, DAYOFWEEK, DAYOFYEAR, HOUR, MINUTE, MONTH, MONTHNAME, QUARTER, SECOND, SUBDATE, TIME_TO_SEC, TO_DAYS, TO_SECONDS, UNIX_TIMESTAMP, WEEK, WEEKDAY, WEEKOFYEAR, YEAR, YEARWEEK) | 动态 |
| **字符串函数** | 28 个 (ASCII, BIN, BIT_LENGTH, CHAR_LENGTH, CONCAT, CONCAT_WS, FIELD, FIND_IN_SET, FORMAT, HEX, INSTR, LCASE, LEFT, LENGTH, LOCATE, LOWER, LPAD, LTRIM, MD5, OCT, ORD, QUOTE, REPEAT, REPLACE, REVERSE, RIGHT, RPAD, RTRIM, SHA1, SHA2, SOUNDEX, SPACE, STRCMP, SUBSTRING, TO_BASE64, TRIM, UCASE, UPPER) | 动态 |
| **条件函数** | 8 个 (IF, IFNULL, ISNULL, NULLIF, GREATEST, LEAST, INTERVAL, MAKE_SET) | 动态 |
| **比较函数** | 6 个 (GREATEST, LEAST, INTERVAL × 2 类型) | 动态 |
| **聚合函数** | 31 个 (AVG, COUNT, MAX, MIN, SUM, STDDEV_POP/SAMP, VAR_POP/SAMP, BIT_AND/OR/XOR, ANY_VALUE, GROUP_CONCAT, JSON_ARRAYAGG, JSON_OBJECTAGG) | 动态 |
| **窗口函数（排名）** | 6 个 (CUME_DIST, DENSE_RANK, NTILE, RANK, ROW_NUMBER, PERCENT_RANK) | 动态 |
| **窗口函数（值）** | 9 个 (FIRST_VALUE×3, LAST_VALUE×3, LAG×3, LEAD×3) | 动态 |
| **MySQL 独有** | IF, IFNULL, ISNULL, CRC32, MD5, SHA1, SHA2, SOUNDEX, GROUP_CONCAT, ANY_VALUE, JSON_ARRAYAGG, JSON_OBJECTAGG, BIT_COUNT, FIND_IN_SET | — |
| **PostgreSQL 独有** | — | 从 pg_catalog 自动发现所有内置函数 |

### 6. JOIN 类型对比

| JOIN 类型 | MySQL | PostgreSQL |
|-----------|:-----:|:----------:|
| INNER | ✅ | ✅ |
| LEFT OUTER | ✅ | ✅ |
| RIGHT OUTER | ✅ | ✅ |
| CROSS | ✅ | ✅ |
| FULL OUTER | ❌ | ✅ |
| STRAIGHT_JOIN | ✅ | ❌ |

### 7. 复合运算符对比

| 运算符 | MySQL | PostgreSQL |
|--------|:-----:|:----------:|
| UNION | ✅ (union distinct) | ✅ (union) |
| UNION ALL | ✅ | ✅ |
| INTERSECT | ✅* | ✅ |
| INTERSECT ALL | ❌ | ✅ |
| EXCEPT | ✅* | ✅ |
| EXCEPT ALL | ❌ | ✅ |

> \*MySQL 8.0.31+ 支持，但 flag 默认关闭

### 8. 语句生成差异

| 语句 | MySQL | PostgreSQL | 说明 |
|------|:-----:|:----------:|------|
| SELECT | ✅ | ✅ | 通用 |
| SELECT FOR UPDATE | ✅ | ✅ | PG 支持更多锁模式 (4 种 vs 2 种) |
| INSERT | ✅ | ✅ | 通用 |
| INSERT SELECT | ✅ | ✅ | 通用 |
| UPDATE | ✅ + ORDER BY/LIMIT | ✅ | MySQL 独有 ORDER BY+LIMIT |
| DELETE | ✅ + ORDER BY/LIMIT | ✅ | MySQL 独有 ORDER BY+LIMIT |
| DELETE RETURNING | ❌ | ✅ | PG 独有 |
| UPDATE RETURNING | ❌ | ✅ | PG 独有 |
| UPSERT | ON DUPLICATE KEY | ON CONFLICT | 语法不同 |
| MERGE | ❌ | ✅ (PG 15+) | PG 独有 |
| REPLACE | ✅ | ❌ | MySQL 独有 |
| DO | ✅ | ❌ | MySQL 独有 |
| EXPLAIN | ✅ | ❌* | MySQL 独有生成 |
| 表维护 | ✅ | ❌ | MySQL 独有 (CHECKSUM/CHECK/OPTIMIZE/REPAIR) |
| 数据修改 CTE | ❌ | ✅ | PG 独有 |
| Index Hints | ✅ | ❌ | MySQL 独有 (USE/FORCE/IGNORE INDEX) |
| SELECT 选项 | ✅ | ❌ | MySQL 独有 (SQL_CALC_FOUND_ROWS 等) |
| GROUP BY WITH ROLLUP | ✅ | ❌ | MySQL 独有 |

> \*PostgreSQL 原生支持 EXPLAIN，但 dbfuzz 未为其生成 explain_stmt

### 9. SET 参数对比

| 维度 | MySQL | PostgreSQL |
|------|-------|------------|
| **参数数量** | 18 个 optimizer_switch 选项 | 20+ 个 Planner 方法参数 + 17 成本参数 + 6 GEQO 参数 + 3 其他 |
| **设置方式** | `SET SESSION optimizer_switch = 'flag=on/off'` | `SET enable_xxx = on/off` |
| **覆盖范围** | 优化器策略开关 | 扫描方法、并行执行、JIT、GEQO 遗传优化器、成本参数 |

### 10. 错误处理对比

| 维度 | MySQL | PostgreSQL |
|------|-------|------------|
| **错误模式数** | ~35 个正则模式 | 从 `pgsqlerr.txt` 加载 + 4 种崩溃检测 |
| **崩溃检测** | `Lost connection` 正则 | `PQstatus == CONNECTION_BAD` + 4 种连接断开模式 |
| **死锁处理** | `Deadlock found` → txn_abort | 通用错误处理 |
| **超时机制** | MySQL 8.x nonblocking API 100ms 轮询 | `SET statement_timeout = 6s` |
| **锁阻塞检测** | `sys.innodb_lock_waits` 轮询 | 依赖 statement_timeout |
| **预期错误** | Duplicate entry, Unknown column, FK constraint, OOM, key too long, 30+ 种 | 超时、语法、连接断开 |

### 11. DUT（数据库驱动）对比

| 能力 | MySQL (`dut_mysql`) | PostgreSQL (`dut_libpq`) |
|------|---------------------|--------------------------|
| **连接方式** | libmysqlclient (C API) | libpq (C API) |
| **异步查询** | ✅ `mysql_real_query_nonblocking` | ❌ PQexec 同步 |
| **事务控制** | START TRANSACTION / COMMIT / ROLLBACK | BEGIN / COMMIT / ROLLBACK |
| **备份** | `mysqldump` 命令 | 复制 db_setup.sql |
| **恢复** | `mysql < backup.sql` | 逐行读取并执行 SQL |
| **进程管理** | `fork()` + `execv(mysqld)` | `fork()` + `execv(postgres)` |
| **线程 ID** | `mysql_thread_id()` | — |
| **阻塞检测** | ✅ `sys.innodb_lock_waits` | ❌ |
| **标识符引用** | 反引号 `` `name` `` | 无引用 `name` |

---

## 三、各测试模式的支持矩阵

### 3.1 Smoke 模式

| 功能 | PostgreSQL | MySQL |
|------|:----------:|:-----:|
| 随机 SQL 生成 | ✅ | ✅ |
| 事务包裹 (ROLLBACK→BEGIN→stmt→ROLLBACK) | ✅ | ✅ |
| 会话变量设置 (statement_timeout) | ✅ | ⚠️ 部分 |
| 阻抗反馈 | ✅ | ✅ |
| 已知错误过滤 | ✅ | ✅ |
| GraphML AST dump | ✅ | ✅ |
| RNG 状态序列化 | ✅ | ✅ |
| 连接恢复 | ✅ | ✅ |
| PG 高级语法 (数据修改CTE/JSON/ARRAY/GROUPING SETS) | ✅ | ❌ |
| MySQL 特有语法 (REPLACE/EXPLAIN/Index Hints/WITH ROLLUP) | ❌ | ✅ |

### 3.2 EET 模式

| 功能 | PostgreSQL | MySQL |
|------|:----------:|:-----:|
| QCN SELECT/UPDATE/DELETE/CTE/INSERT-SELECT 测试 | ✅ | ✅ |
| 等价表达式变换 | ✅ | ✅ |
| REGEXP/SOUNDS LIKE 表达式 | ❌ | ✅ |
| 优化器设置随机注入 | ✅ (20+ GUC) | ✅ (18 optimizer_switch) |
| 数据库生成/备份/恢复 | ✅ | ✅ |
| 用例最小化 | ✅ | ✅ |

### 3.3 TxCheck 模式

| 功能 | PostgreSQL | MySQL |
|------|:----------:|:-----:|
| 事务生成 + 依赖图分析 | ✅ | ✅ |
| 异常检测 (G1a/G1b/G1c/G2-item/G-SI) | ✅ | ✅ |
| 非阻塞执行 | ❌ | ✅ |
| 阻塞检测 | ❌ | ✅ |
| 服务器 fork/重启 | ✅ | ✅ |
| Bug 复现 + 最小化 | ✅ | ✅ |

### 3.4 Cross 模式

| 功能 | PostgreSQL | MySQL |
|------|:----------:|:-----:|
| 跨库 SQL 执行 + 结果 diff | ✅ | ✅ |
| 假阳性过滤 | ✅ | ✅ |
| Bug 报告 + 最小化 | ✅ | ✅ |

---

## 四、PostgreSQL 优势领域

| 领域 | 详情 |
|------|------|
| **动态元数据** | 函数/操作符/聚合从 pg_catalog 自动发现，随版本自动扩展 |
| **类型系统** | 完整 OID 类型系统 + 14 种伪类型 |
| **高级 SQL** | 数据修改 CTE、RETURNING、MERGE、TABLESAMPLE、量化比较 |
| **JSON/数组** | 原生 JSON/JSONB 类型 + 数组操作符 |
| **窗口函数** | 完整 FRAME 子句（ROWS/RANGE/GROUPS） |
| **GROUP BY** | GROUPING SETS / CUBE / ROLLUP |
| **集合运算** | INTERSECT ALL / EXCEPT ALL |
| **锁查询** | 4 种锁模式（UPDATE/SHARE/NO KEY UPDATE/KEY SHARE） |
| **Planner 控制** | 20+ 方法参数 + 17 成本参数 + 6 GEQO 参数 |

## 五、MySQL 优势领域

| 领域 | 详情 |
|------|------|
| **REPLACE 语句** | 原生 REPLACE INTO |
| **Index Hints** | USE/FORCE/IGNORE INDEX（优化器提示） |
| **STRAIGHT_JOIN** | 强制连接顺序 |
| **SELECT 选项** | SQL_CALC_FOUND_ROWS, SQL_NO_CACHE, HIGH_PRIORITY 等 |
| **GROUP BY WITH ROLLUP** | 汇总行生成 |
| **DO 语句** | 表达式求值（无结果集） |
| **EXPLAIN 变体** | FORMAT=JSON/TREE/TRADITIONAL/ANALYZE |
| **表维护** | CHECKSUM/CHECK/OPTIMIZE/REPAIR TABLE |
| **REGEXP/SOUNDS LIKE** | 正则和语音匹配操作符 |
| **NULL-safe 等于** | `<=>` 操作符 |
| **UPDATE/DELETE ORDER BY+LIMIT** | 限制影响行数 |
| **异步查询 + 锁检测** | nonblocking API + sys.innodb_lock_waits |
| **ON DUPLICATE KEY UPDATE** | 不同于 PG ON CONFLICT 的 UPSERT 语义 |

---

## 六、量化总结

| 指标 | MySQL | PostgreSQL | 说明 |
|------|:-----:|:----------:|------|
| Feature Flags 启用 | **15**/26 | **14**/26 | MySQL 数量更多但类型不同 |
| 操作符数量 | 36（硬编码） | 50-100+（动态） | PG 更广 |
| 标量函数 | ~80（硬编码） | 100-200+（动态） | PG 更广 |
| 聚合函数 | 31（硬编码） | 30-50+（动态） | 接近 |
| 窗口函数 | 15（硬编码） | 动态 | PG 更广 |
| JOIN 类型 | 5 | 5 | 持平（各有独有类型） |
| 复合运算符 | 4 | 6 | PG +2 |
| 独有语句 | 4 (REPLACE/DO/EXPLAIN/表维护) | 2 (MERGE/数据修改CTE) | MySQL +2 |
| SET 参数 | 18 | 20+（+23 成本/GEQO） | PG 更广 |
| 错误正则模式 | ~35 | ~12 | MySQL 更细 |
| 类型覆盖 | 5 规范 + 20 别名 | 200+ OID | 架构不同 |

---

## 七、改进建议

### 优先级 P0（高价值）
1. **MySQL 动态元数据加载**：从 `mysql.help_topic` 或 `SHOW BUILTIN FUNCTIONS` 加载函数签名
2. **PostgreSQL EXPLAIN 生成**：为 PG 实现 explain_stmt 生成
3. **MySQL INTERSECT/EXCEPT flag 开启**：MySQL 8.0.31+ 已支持

### 优先级 P1（中等价值）
4. **MySQL 空间函数**：ST_Contains, ST_Distance 等 GIS 函数
5. **MySQL JSON 函数**：JSON_EXTRACT, JSON_SET, JSON_ARRAY 等
6. **MySQL 递归 CTE 增强**：WITH RECURSIVE 完整支持
7. **补全未检查 Flag**：为 has_merge/has_returning/has_tablesample 添加 if 检查

### 优先级 P2（低价值）
8. **MySQL HANDLER 语句**：直接表访问接口
9. **MySQL PARTITION 选择**：SELECT ... PARTITION (p0, p1)
10. **TiDB/MariaDB/OceanBase 差异化**：从 mysql.cc 继承中分离出各 DBMS 特有语法
