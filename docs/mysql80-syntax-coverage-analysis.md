# MySQL 8.0 语法覆盖度分析报告

> 分析日期：2026-06-11  
> 对比基准：MySQL 8.0 Reference Manual  
> 分析对象：dbfuzz v1.0.19 所有运行模式（EET/TxCheck/Cross/Smoke）

---

## 总览

| 类别 | MySQL 8.0 特性数 | 已覆盖 | 覆盖率 | 优先级评估 |
|------|-----------------|--------|--------|-----------|
| DML 语句 | 15+ | 9 | **60%** | ⭐⭐⭐ 高 |
| DDL 语句 | 30+ | 6 | **20%** | ⭐⭐ 中 |
| 事务与锁 | 12 | 5 | **42%** | ⭐⭐⭐ 高 |
| 预处理语句 | 3 | 2 | **67%** | ⭐ 低 |
| 比较运算符 | 10 | 9 | **90%** | ✅ 充足 |
| 逻辑/算术/位运算符 | 16 | 15 | **94%** | ✅ 充足 |
| 字符串函数 | 60+ | 40 | **67%** | ⭐⭐ 中 |
| 数值函数 | 30 | 25 | **83%** | ✅ 充足 |
| 日期/时间函数 | 50+ | 32 | **64%** | ⭐⭐ 中 |
| JSON 函数 | 32 | 16 | **50%** | ⭐⭐⭐ 高 |
| 窗口函数 | 12+ | 12 | **100%** | ✅ 完整 |
| 聚合函数 | 20+ | 15 | **75%** | ⭐ 低 |
| 子查询 | 6 种 | 4 | **67%** | ⭐⭐⭐ 高 |
| 高级特性 | 10+ | 3 | **30%** | ⭐⭐⭐ 高 |
| 数据类型 | 30+ | 10 | **33%** | ⭐⭐ 中 |
| SHOW/管理语句 | 40+ | 5 | **12%** | ⭐ 低 |
| 存储程序 | 15+ | 1 (TRIGGER) | **7%** | ⭐ 低 |

**综合覆盖率：约 45%**（核心 DML/表达式覆盖较好，DDL/高级特性/函数缺口较大）

---

## 一、DML 语句差距

### 1.1 已实现 ✅

| 语句 | 类名 | 备注 |
|------|------|------|
| SELECT（全子句） | `query_spec` | DISTINCT, WHERE, GROUP BY, HAVING, ORDER BY, LIMIT, FOR UPDATE/SHARE |
| INSERT VALUES | `insert_stmt` | 基本 INSERT |
| INSERT SELECT | `insert_select_stmt` | INSERT ... SELECT |
| INSERT ON DUPLICATE KEY UPDATE | `upsert_stmt` | MySQL UPSERT |
| UPDATE（单表） | `update_stmt` | 含 ORDER BY + LIMIT |
| DELETE（单表） | `delete_stmt` | 含 ORDER BY + LIMIT |
| REPLACE | `replace_stmt` | MySQL REPLACE |
| DO | `do_stmt` | MySQL DO |
| UNION/INTERSECT/EXCEPT | `unioned_query` | 含 ALL/DISTINCT |

### 1.2 缺失 ❌（高优先级）

| 缺失特性 | MySQL 语法 | 影响模式 | 建议优先级 |
|---------|-----------|---------|-----------|
| **多表 UPDATE** | `UPDATE t1, t2 SET t1.a=t2.b WHERE t1.id=t2.id` | EET/TxCheck | 🔴 **P0** |
| **多表 DELETE** | `DELETE t1 FROM t1 JOIN t2 ON ... WHERE ...` | EET/TxCheck | 🔴 **P0** |
| **INSERT SET 形式** | `INSERT INTO t SET col1=v1, col2=v2` | EET | 🟡 P1 |
| **INSERT IGNORE** | `INSERT IGNORE INTO t ...` | TxCheck | 🟡 P1 |
| **INSERT LOW/HIGH_PRIORITY** | `INSERT LOW_PRIORITY INTO t ...` | Smoke | 🟢 P2 |
| **VALUES() 引用** | `ON DUPLICATE KEY UPDATE col = VALUES(col) + 1` | EET/TxCheck | 🟡 P1 |
| **INSERT 多行 VALUES** | `INSERT INTO t VALUES (1,2), (3,4), (5,6)` | EET | 🟡 P1 |
| **SELECT FOR UPDATE NOWAIT** | `SELECT ... FOR UPDATE NOWAIT` | TxCheck | 🟡 P1 |
| **SELECT SKIP LOCKED** | `SELECT ... FOR UPDATE SKIP LOCKED` | TxCheck | 🟡 P1 |
| **SELECT FOR UPDATE OF tbl** | `SELECT ... FOR UPDATE OF t1` | TxCheck | 🟢 P2 |

### 1.3 缺失 ❌（低优先级）

| 缺失特性 | MySQL 语法 | 说明 |
|---------|-----------|------|
| LOAD DATA INFILE | `LOAD DATA INFILE 'file' INTO TABLE t` | 文件操作，fuzzer 不适用 |
| HANDLER | `HANDLER t OPEN; HANDLER t READ FIRST;` | 低级存储引擎接口 |
| CALL | `CALL proc_name(args)` | 需要存储过程 |
| IMPORT TABLE | `IMPORT TABLE FROM sdi_file` | MySQL 8.0 内部操作 |

---

## 二、DDL 语句差距

### 2.1 已实现 ✅

| 语句 | 类名 | 备注 |
|------|------|------|
| CREATE TABLE | `create_table_stmt` | 含分区、表选项、FK |
| CREATE VIEW | `create_view_stmt` | 基本 CREATE VIEW |
| CREATE INDEX | `create_index_stmt` | 含 UNIQUE、partial index |
| CREATE TRIGGER | `create_trigger_stmt` | BEFORE/AFTER INSERT/UPDATE/DELETE |
| ALTER TABLE（多种） | `alter_table_stmt` | RENAME/ADD/DROP/MODIFY COLUMN, ADD INDEX, 分区管理 |
| DROP TABLE | `drop_table_stmt` | IF EXISTS |

### 2.2 缺失 ❌（高优先级）

| 缺失特性 | MySQL 语法 | 影响 | 建议优先级 |
|---------|-----------|------|-----------|
| **CHECK 约束** | `CREATE TABLE t (col INT CHECK (col > 0))` | MySQL 8.0 强制执行 | 🔴 **P0** |
| **生成列** | `col INT GENERATED ALWAYS AS (expr) STORED/VIRTUAL` | MySQL 5.7+ | 🔴 **P0** |
| **TRUNCATE TABLE** | `TRUNCATE TABLE t` | 常见 DDL | 🟡 P1 |
| **DROP INDEX** | `DROP INDEX idx ON t` | DDL 完整性 | 🟡 P1 |
| **CREATE TABLE LIKE** | `CREATE TABLE t2 LIKE t1` | Schema 复制 | 🟡 P1 |
| **CREATE TABLE SELECT** | `CREATE TABLE t AS SELECT ...` | CTAS | 🟡 P1 |
| **ALTER TABLE CONVERT CHARSET** | `ALTER TABLE t CONVERT TO CHARACTER SET utf8mb4` | 字符集操作 | 🟢 P2 |
| **ALTER TABLE ADD CHECK** | `ALTER TABLE t ADD CONSTRAINT chk CHECK (expr)` | 约束管理 | 🟡 P1 |

### 2.3 缺失 ❌（低优先级 — 不适合 fuzzer）

| 缺失特性 | 说明 |
|---------|------|
| CREATE/ALTER/DROP DATABASE | dbfuzz 在固定数据库中运行，不需要 |
| CREATE/ALTER/DROP PROCEDURE | 存储过程超出 fuzzer 范围 |
| CREATE/ALTER/DROP FUNCTION | 同上 |
| CREATE/ALTER/DROP EVENT | 定时事件，不适合随机测试 |
| CREATE/ALTER/DROP TABLESPACE | 表空间管理 |
| CREATE SPATIAL REFERENCE SYSTEM | 空间参考系统 |
| ALTER VIEW / DROP VIEW | 视图管理 |
| DROP TRIGGER | 触发器管理 |

---

## 三、事务与锁差距

### 3.1 已实现 ✅

- START TRANSACTION / COMMIT / ROLLBACK
- SAVEPOINT / RELEASE SAVEPOINT / ROLLBACK TO SAVEPOINT
- LOCK TABLES / UNLOCK TABLES
- SELECT FOR UPDATE / FOR SHARE

### 3.2 缺失 ❌（高优先级）

| 缺失特性 | MySQL 语法 | 影响 | 建议优先级 |
|---------|-----------|------|-----------|
| **XA 事务** | `XA START/END/PREPARE/COMMIT/ROLLBACK 'xid'` | 分布式事务 Bug 检测 | 🔴 **P0** |
| **SET TRANSACTION ISOLATION** | `SET SESSION TRANSACTION ISOLATION LEVEL ...` | 隔离级别切换测试 | 🔴 **P0** |
| **START TRANSACTION 选项** | `START TRANSACTION WITH CONSISTENT SNAPSHOT / READ ONLY` | 事务语义测试 | 🟡 P1 |
| **FOR UPDATE NOWAIT** | `SELECT ... FOR UPDATE NOWAIT` | 锁等待行为测试 | 🟡 P1 |
| **FOR UPDATE SKIP LOCKED** | `SELECT ... FOR UPDATE SKIP LOCKED` | 并发跳过测试 | 🟡 P1 |

### 3.3 缺失 ❌（低优先级）

| 缺失特性 | 说明 |
|---------|------|
| LOCK INSTANCE FOR BACKUP | 实例级备份锁 |
| COMMIT AND CHAIN / RELEASE | 事务链/释放 |
| GET_LOCK / RELEASE_LOCK | 命名锁（非事务级） |

---

## 四、表达式与函数差距

### 4.1 运算符覆盖（90%+ — 充足）

| 运算符 | 状态 | 备注 |
|--------|------|------|
| `= <> < > <= >= !=` | ✅ | 全部支持 |
| `<=>` (NULL-safe equal) | ✅ | MySQL 特有 |
| `DIV` (整数除法) | ✅ | MySQL 特有 |
| `REGEXP / RLIKE` | ✅ | 含 NOT REGEXP |
| `SOUNDS LIKE` | ✅ | MySQL 特有 |
| `MEMBER OF()` | ✅ | MySQL 8.0.17+ |
| `BETWEEN ... AND ...` | ✅ | |
| `IN / NOT IN` | ✅ | 含子查询 |
| `LIKE / NOT LIKE` | ✅ | |
| `IS NULL / IS NOT NULL` | ✅ | |
| `EXISTS` | ✅ | |
| `IS TRUE / IS FALSE` | ❌ | 缺失 |
| `:=` (赋值运算符) | ❌ | 缺失 |

### 4.2 字符串函数差距（40/60 = 67%）

**已实现（40 个）**：
ASCII, BIN, BIT_LENGTH, CHAR_LENGTH, CONCAT, CONCAT_WS, FIELD, FIND_IN_SET, FORMAT, HEX, INSTR, LCASE, LEFT, LENGTH, LOCATE, LOWER, LPAD, LTRIM, MAKE_SET, MD5, OCT, ORD, QUOTE, REGEXP_REPLACE, REGEXP_INSTR, REGEXP_SUBSTR, REPEAT, REPLACE, REVERSE, RIGHT, RPAD, RTRIM, SOUNDEX, SPACE, STRCMP, SUBSTRING, TO_BASE64, TRIM, UCASE, UPPER

**缺失（高优先级）**：

| 函数 | 说明 | 优先级 |
|------|------|--------|
| `CHAR(n1,n2,...)` | 从 ASCII 码生成字符 | 🟡 P1 |
| `ELT(n,str1,str2,...)` | 返回第 N 个字符串 | 🟡 P1 |
| `INSERT(str,pos,len,new)` | 插入子串 | 🟡 P1 |
| `SUBSTRING_INDEX(str,delim,n)` | 按分隔符取子串 | 🟡 P1 |
| `REGEXP_LIKE(str,regex)` | MySQL 8.0 正则匹配函数 | 🟡 P1 |
| `FROM_BASE64(str)` | Base64 解码（已有 TO_BASE64） | 🟡 P1 |
| `UNHEX(str)` | 十六进制解码（已有 HEX） | 🟢 P2 |

**缺失（低优先级）**：
EXPORT_SET, LOAD_FILE, MID, OCTET_LENGTH, POSITION, WEIGHT_STRING

### 4.3 日期/时间函数差距（32/50 = 64%）

**已实现（32 个）**：
ADDDATE, CURDATE, CURTIME, DATE_FORMAT, DATEDIFF, DAYOFMONTH, DAYOFWEEK, DAYOFYEAR, HOUR, LAST_DAY, MAKEDATE, MAKETIME, MINUTE, MONTH, MONTHNAME, NOW, QUARTER, SECOND, STR_TO_DATE, SUBDATE, SYSDATE, TIME_TO_SEC, TO_DAYS, TO_SECONDS, UNIX_TIMESTAMP, UTC_DATE, UTC_TIME, UTC_TIMESTAMP, WEEK, WEEKDAY, WEEKOFYEAR, YEAR, YEARWEEK

**缺失（高优先级）**：

| 函数 | 说明 | 优先级 |
|------|------|--------|
| `ADDTIME(t1,t2)` | 时间加法 | 🟡 P1 |
| `SUBTIME(t1,t2)` | 时间减法 | 🟡 P1 |
| `DATE_ADD(date,INTERVAL)` | 标准 INTERVAL 加法 | 🟡 P1 |
| `DATE_SUB(date,INTERVAL)` | 标准 INTERVAL 减法 | 🟡 P1 |
| `EXTRACT(unit FROM date)` | 提取日期部分 | 🟡 P1 |
| `CONVERT_TZ(dt,from_tz,to_tz)` | 时区转换 | 🟡 P1 |
| `TIMESTAMPDIFF(unit,dt1,dt2)` | 时间戳差值 | 🟡 P1 |
| `TIMESTAMPADD(unit,n,dt)` | 时间戳加法 | 🟡 P1 |
| `DAYNAME(date)` | 星期名（已有 MONTHNAME） | 🟢 P2 |
| `MICROSECOND(expr)` | 微秒 | 🟢 P2 |

**缺失（低优先级）**：
DATE, TIME, DAY, FROM_DAYS, FROM_UNIXTIME, GET_FORMAT, LOCALTIME, LOCALTIMESTAMP, PERIOD_ADD, PERIOD_DIFF, SEC_TO_TIME, TIME_FORMAT, TIMEDIFF, TIMESTAMP

### 4.4 JSON 函数差距（16/32 = 50% — 高缺口）

**已实现（16 个）**：
JSON_ARRAY, JSON_OBJECT, JSON_QUOTE, JSON_EXTRACT, JSON_CONTAINS, JSON_KEYS, JSON_SET, JSON_INSERT, JSON_REPLACE, JSON_REMOVE, JSON_MERGE_PATCH, JSON_DEPTH, JSON_LENGTH, JSON_TYPE, JSON_UNQUOTE, `->`/`->>` 操作符, MEMBER OF

**缺失（高优先级）**：

| 函数 | 说明 | 优先级 |
|------|------|--------|
| `JSON_TABLE(doc, path COLUMNS ...)` | **JSON 转关系表（表函数）** | 🔴 **P0** |
| `JSON_ARRAY_APPEND(doc,path,val)` | 数组追加 | 🟡 P1 |
| `JSON_ARRAY_INSERT(doc,path,val)` | 数组插入 | 🟡 P1 |
| `JSON_CONTAINS_PATH(doc,one/all,path)` | 路径存在性检查 | 🟡 P1 |
| `JSON_OVERLAPS(doc1,doc2)` | JSON 重叠检查（8.0.17+） | 🟡 P1 |
| `JSON_SEARCH(doc,one/all,str)` | 字符串搜索 | 🟡 P1 |
| `JSON_MERGE_PRESERVE(doc1,doc2)` | 保留重复合并 | 🟢 P2 |
| `JSON_VALUE(doc,path)` | 标量提取（8.0.21+） | 🟢 P2 |

**缺失（低优先级）**：
JSON_PRETTY, JSON_SCHEMA_VALID, JSON_SCHEMA_VALIDATION_REPORT, JSON_STORAGE_FREE, JSON_STORAGE_SIZE, JSON_VALID, JSON_MERGE (deprecated)

### 4.5 聚合函数差距（15/20 = 75%）

**已实现**：AVG, COUNT, MAX, MIN, SUM, STDDEV_POP, STDDEV_SAMP, VAR_POP, VAR_SAMP, BIT_AND, BIT_OR, BIT_XOR, ANY_VALUE, GROUP_CONCAT, JSON_ARRAYAGG, JSON_OBJECTAGG

**缺失**：

| 函数 | 说明 | 优先级 |
|------|------|--------|
| `COUNT(DISTINCT expr)` | 去重计数 | 🟡 P1 |
| `GROUP_CONCAT(DISTINCT ...)` | 去重连接 | 🟢 P2 |
| `STDDEV(expr)` | STDDEV_POP 别名 | 🟢 P2 |
| `VARIANCE(expr)` | VAR_POP 别名 | 🟢 P2 |
| `GROUPING(col)` | GROUP BY 超聚合标记 | 🟢 P2 |

---

## 五、子查询差距

### 5.1 已实现 ✅

| 类型 | 示例 | 状态 |
|------|------|------|
| 标量子查询 | `(SELECT MAX(col) FROM t)` | ✅ via `atomic_subselect` |
| EXISTS | `EXISTS (SELECT ...)` | ✅ via `exists_predicate` |
| IN 子查询 | `expr IN (SELECT ...)` | ✅ via `in_query`（含 NOT IN） |
| 比较子查询 | `expr = (SELECT ...)` | ✅ via `comp_subquery` |
| 派生表 | `(SELECT ...) AS alias` | ✅ via `table_subquery` |
| LATERAL 派生表 | `LATERAL (SELECT ...) AS alias` | ✅ via `lateral_subquery` |
| 量化比较 | `expr > ALL (SELECT ...)` | ✅ via `quantified_comparison` |

### 5.2 缺失 ❌

| 缺失类型 | 示例 | 影响 | 优先级 |
|---------|------|------|--------|
| **关联子查询** | `WHERE col = (SELECT MAX(c) FROM t2 WHERE t2.id = t1.id)` | 逻辑 Bug 高发区 | 🔴 **P0** |
| **行子查询** | `WHERE (a,b) = (SELECT a,b FROM t2 LIMIT 1)` | 行比较 | 🟡 P1 |
| **ANY/SOME** | `WHERE col > ANY (SELECT col FROM t2)` | 量化比较 | 🟡 P1（quantified_comparison 可能已部分覆盖） |

---

## 六、高级特性差距

### 6.1 已实现 ✅

| 特性 | 说明 |
|------|------|
| CTE (WITH) | 含 RECURSIVE |
| LATERAL 派生表 | MySQL 8.0.14+ |
| 分区表 | RANGE/LIST/HASH/KEY + 子分区 + 管理操作 |
| 窗口函数 + 窗口帧 | 全部 12 个函数 + ROWS/RANGE/GROUPS 帧 |
| ON DUPLICATE KEY UPDATE | MySQL UPSERT |

### 6.2 缺失 ❌（高优先级）

| 缺失特性 | MySQL 语法 | 影响 | 优先级 |
|---------|-----------|------|--------|
| **JSON_TABLE** | `JSON_TABLE(doc, '$[*]' COLUMNS (...)) AS jt` | 表函数，可出现在 FROM 中 | 🔴 **P0** |
| **CHECK 约束** | `CHECK (expr)` | MySQL 8.0 强制执行 | 🔴 **P0** |
| **生成列** | `col AS (expr) STORED/VIRTUAL` | 自动计算列 | 🔴 **P0** |
| **VISIBLE/INVISIBLE 索引** | `CREATE INDEX idx ON t(col) INVISIBLE` | MySQL 8.0 新特性 | 🟡 P1 |
| **函数索引** | `CREATE INDEX idx ON t((LOWER(col)))` | MySQL 8.0.13+ | 🟡 P1 |
| **多值索引** | JSON 数组上的索引 | MySQL 8.0.17+ | 🟢 P2 |
| **EXPLAIN ANALYZE** | `EXPLAIN ANALYZE SELECT ...` | MySQL 8.0.18+ | 🟢 P2 |
| **SET PERSIST** | `SET PERSIST max_connections = 1000` | 持久化变量 | 🟢 P2 |

---

## 七、数据类型差距

### 7.1 已实现 ✅

int, real, text, datetime, bool, blob, json（及 30+ 别名映射）

### 7.2 缺失 ❌

| 缺失类型 | MySQL 语法 | 影响 | 优先级 |
|---------|-----------|------|--------|
| **ENUM** | `ENUM('val1','val2','val3')` | 常见业务类型 | 🔴 **P0** |
| **SET** | `SET('a','b','c')` | MySQL 特有类型 | 🟡 P1 |
| **BIT(n)** | `BIT(8)` | 位类型 | 🟡 P1 |
| **UNSIGNED 整数** | `INT UNSIGNED`, `BIGINT UNSIGNED` | 范围差异易出 Bug | 🟡 P1 |
| **空间类型** | `GEOMETRY`, `POINT`, `LINESTRING`, `POLYGON` | 全新类型域 | 🟢 P2 |
| **DECIMAL(M,D)** | `DECIMAL(10,2)` | 精度/标度 | 🟢 P2 |
| **YEAR** | `YEAR` | 已映射为 datetime | ⚪ 已覆盖 |

---

## 八、各运行模式覆盖度差异

| 模式 | SQL 生成入口 | 覆盖特点 | 主要缺口 |
|------|-------------|---------|---------|
| **EET** | `statement_factory` → SELECT/CTE/UPDATE/DELETE/INSERT-SELECT | 侧重 SELECT 变换，写操作有限 | 无多表 UPDATE/DELETE，无关联子查询 |
| **TxCheck** | `txn_statement_factory` → DELETE/CTE/SELECT/INSERT/UPDATE | 侧重事务内 DML，依赖图分析 | 无 XA 事务，无隔离级别切换，无 FOR UPDATE NOWAIT |
| **Cross** | `cross_tester` → 事务 + EET 变换 | 综合但仅 SELECT 做 EET | 同 EET + TxCheck 的缺口 |
| **Smoke** | `smoke_run` → `statement_factory` | 最高频率，覆盖全部生成器 | 同 EET 的生成器缺口 |

**核心发现**：所有模式共享同一个 SQL 生成器 (`grammar_objs`)，因此生成器层面的缺口影响全部模式。事务层面的缺口（XA、隔离级别）仅影响 TxCheck 和 Cross。

---

## 九、优先级排序（建议实施路线）

### P0 — 高价值缺口（建议立即实现）

| # | 特性 | 影响模式 | 估计工作量 | 预期价值 |
|---|------|---------|-----------|---------|
| 1 | **多表 UPDATE** | EET/TxCheck/Cross/Smoke | 2-3 天 | 🔥 高 — JOIN 更新是 Bug 高发区 |
| 2 | **多表 DELETE** | EET/TxCheck/Cross/Smoke | 2-3 天 | 🔥 高 — 同上 |
| 3 | **关联子查询** | EET/Cross/Smoke | 3-4 天 | 🔥 高 — 逻辑 Bug 检测核心 |
| 4 | **CHECK 约束** | EET/Smoke（DDL 阶段） | 1-2 天 | 🔥 高 — MySQL 8.0 强制特性 |
| 5 | **生成列** | EET/Smoke（DDL 阶段） | 1-2 天 | 🔥 高 — 自动计算列影响查询结果 |
| 6 | **JSON_TABLE** | EET/Cross/Smoke | 2-3 天 | 🔥 高 — 全新表函数类型 |
| 7 | **XA 事务** | TxCheck/Cross | 2-3 天 | 🔥 高 — 分布式事务异常检测 |
| 8 | **SET TRANSACTION ISOLATION** | TxCheck | 1 天 | 🔥 高 — 隔离级别切换测试 |
| 9 | **ENUM 数据类型** | 所有模式 | 2 天 | 🔥 高 — 常见业务类型，边界值丰富 |

### P1 — 中等价值（建议后续迭代）

| # | 特性 | 估计工作量 |
|---|------|-----------|
| 10 | INSERT SET 形式 | 1 天 |
| 11 | INSERT 多行 VALUES | 1 天 |
| 12 | VALUES() 引用 (ON DUPLICATE KEY) | 1 天 |
| 13 | TRUNCATE TABLE | 0.5 天 |
| 14 | CREATE TABLE LIKE / SELECT | 1 天 |
| 15 | FOR UPDATE NOWAIT / SKIP LOCKED | 1 天 |
| 16 | JSON 函数扩展 (+8 个) | 2 天 |
| 17 | 日期函数扩展 (+10 个) | 2 天 |
| 18 | 字符串函数扩展 (+7 个) | 1 天 |
| 19 | BIT / SET / UNSIGNED 类型 | 2 天 |
| 20 | VISIBLE/INVISIBLE 索引 | 1 天 |
| 21 | 函数索引 | 1 天 |
| 22 | IS TRUE / IS FALSE | 0.5 天 |
| 23 | COUNT(DISTINCT expr) | 0.5 天 |
| 24 | 行子查询 | 1 天 |
| 25 | START TRANSACTION 选项 | 1 天 |

### P2 — 低优先级（锦上添花）

空间类型/函数、存储程序、管理语句 (SHOW/FLUSH/KILL)、REPLICATION、LOAD DATA、HANDLER、LOCK INSTANCE 等。

---

## 十、结论

dbfuzz 当前对 MySQL 8.0 的语法覆盖度约 **45%**，核心 DML 和运算符覆盖良好（90%+），但在以下方面存在显著缺口：

1. **多表操作**（多表 UPDATE/DELETE）— 最常见的 Bug 场景之一
2. **关联子查询** — 逻辑 Bug 检测的核心缺口
3. **MySQL 8.0 新特性**（CHECK 约束、生成列、JSON_TABLE、ENUM）
4. **事务高级特性**（XA 事务、隔离级别切换）
5. **JSON 函数**（仅覆盖 50%，JSON_TABLE 完全缺失）

按 P0 优先级实施 9 项特性后，预计覆盖率可提升至 **65-70%**，显著增强 Bug 发现能力。
