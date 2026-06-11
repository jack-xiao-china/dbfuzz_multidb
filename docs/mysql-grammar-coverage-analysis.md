# dbfuzz MySQL 语法覆盖率分析（对照 MySQL 8.4 官方文档）

> 版本：v1.0.14 | 日期：2026-06-09
> 参考：[MySQL 8.4 Reference Manual](https://dev.mysql.com/doc/refman/8.4/en/)

---

## 一、数据类型覆盖率

### 已覆盖（5 个规范类型 + 20 别名）

| 规范类型 | 别名（→ 映射到规范类型） | MySQL 8.4 对应 |
|---------|------------------------|---------------|
| `tinyint` (booltype) | boolean, bool, tinyint | TINYINT |
| `int` (inttype) | bigint, mediumint, smallint, integer | INT/BIGINT/SMALLINT/MEDIUMINT |
| `double` (realtype) | float, decimal, numeric, real | FLOAT/DOUBLE/DECIMAL |
| `varchar(200)` (texttype) | varchar, char, text, mediumtext, longtext, tinytext, enum, set | CHAR/VARCHAR/TEXT 族 |
| `DATETIME` (datetype) | datetime, timestamp, date, time, year | DATE/TIME/DATETIME/TIMESTAMP/YEAR |

### ❌ 缺失的数据类型

| 类型 | MySQL 8.4 说明 | 优先级 | 影响 |
|------|---------------|:------:|------|
| **JSON** | 原生 JSON 数据类型，MySQL 8.0+ 核心特性 | 🔴 P0 | 无法测试 JSON 列和 JSON 函数 |
| **BLOB** | 二进制大对象 (TINYBLOB/BLOB/MEDIUMBLOB/LONGBLOB) | 🟡 P1 | 无法测试二进制数据存储 |
| **BINARY/VARBINARY** | 定长/变长二进制串 | 🟡 P1 | 无法测试二进制比较 |
| **BIT(N)** | 位字段类型 | 🟢 P2 | 位运算场景覆盖不足 |
| **GEOMETRY** | 空间数据类型 (POINT/LINESTRING/POLYGON) | 🟡 P1 | 无法测试空间函数 |
| **UNSIGNED 变体** | INT UNSIGNED, BIGINT UNSIGNED 等 | 🟡 P1 | 无符号整数边界测试缺失 |
| **DECIMAL(M,D)** | 精确小数（带精度和标度） | 🟢 P2 | 精度/标度组合测试缺失 |

### 扩展建议

```cpp
// P0: JSON 类型
auto jsontype = sqltype::get("json");
sqltype::typemap["json"] = jsontype;

// P1: BLOB/BINARY 类型
auto blobtype = sqltype::get("blob");
for (auto &alias : {"tinyblob", "mediumblob", "longblob", "binary", "varbinary"})
    sqltype::typemap[alias] = blobtype;

// P1: 空间类型
auto geometrytype = sqltype::get("geometry");
for (auto &alias : {"point", "linestring", "polygon",
                     "multipoint", "multilinestring", "multipolygon",
                     "geometrycollection"})
    sqltype::typemap[alias] = geometrytype;
```

---

## 二、表类型/存储引擎

### 已覆盖

| 功能 | 状态 | 说明 |
|------|:----:|------|
| ENGINE 选项 | ❌ 未填充 | `supported_table_engine` 向量为空 |
| 表选项 | ❌ 未填充 | `available_table_options` 向量为空 |
| PRIMARY KEY | ✅ | create_table_stmt 生成主键 |
| FOREIGN KEY | ✅ | REFERENCES 约束 |
| CHECK 约束 | ⚠️ 默认关闭 | `has_check` flag 未设置 |
| COLLATE | ✅ | 列级 COLLATE |

### ❌ 缺失的表特性

| 特性 | MySQL 8.4 说明 | 优先级 |
|------|---------------|:------:|
| **ENGINE 指定** | InnoDB/MyISAM/MEMORY/CSV/ARCHIVE | 🟡 P1 |
| **CHARACTER SET** | utf8mb4/latin1/gbk 等字符集 | 🟡 P1 |
| **ROW_FORMAT** | DYNAMIC/COMPACT/COMPRESSED/REDUNDANT | 🟢 P2 |
| **AUTO_INCREMENT** | 自增起始值 | 🟢 P2 |
| **COMMENT** | 表/列注释 | 🟢 P2 |
| **PARTITION BY** | RANGE/LIST/HASH/KEY 分区 | 🟡 P1 |
| **TABLESPACE** | 表空间指定 | 🟢 P2 |
| **SECONDARY_ENGINE** | MySQL 8.0 辅助引擎 | 🟢 P2 |

### MySQL 8.4 存储引擎对比

| 引擎 | 事务 | 行锁 | 外键 | FULLTEXT | SPATIAL | dbfuzz 支持 |
|------|:----:|:----:|:----:|:--------:|:-------:|:-----------:|
| **InnoDB** | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ 未指定 |
| **MyISAM** | ❌ | ❌ | ❌ | ✅ | ✅ | ❌ 未指定 |
| **MEMORY** | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ 未指定 |
| **CSV** | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ 未指定 |
| **ARCHIVE** | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ 未指定 |

### 扩展建议

```cpp
// mysql.cc 构造函数中添加
supported_table_engine.push_back("InnoDB");
supported_table_engine.push_back("MyISAM");
supported_table_engine.push_back("MEMORY");

available_table_options.push_back("CHARSET=utf8mb4");
available_table_options.push_back("CHARSET=latin1");
available_table_options.push_back("ROW_FORMAT=DYNAMIC");
available_table_options.push_back("ROW_FORMAT=COMPACT");
```

---

## 三、索引类型

### 已覆盖

| 索引类型 | 状态 | 说明 |
|---------|:----:|------|
| B-Tree 普通索引 | ✅ | CREATE INDEX |
| B-Tree UNIQUE 索引 | ✅ | CREATE UNIQUE INDEX |
| ASC/DESC 排序 | ✅ | 索引列方向 |
| COLLATE 索引列 | ✅ | 索引列 COLLATE |
| 部分索引 (WHERE) | ❌ 关闭 | `enable_partial_index = false` |

### ❌ 缺失的索引类型

| 索引类型 | MySQL 8.4 语法 | 优先级 |
|---------|---------------|:------:|
| **FULLTEXT 索引** | `CREATE FULLTEXT INDEX idx ON t(col)` | 🟡 P1 |
| **SPATIAL 索引** | `CREATE SPATIAL INDEX idx ON t(col)` | 🟡 P1 |
| **函数索引** (8.0.13+) | `CREATE INDEX idx ON t((CAST(col->>'$.x' AS UNSIGNED)))` | 🟡 P1 |
| **多值索引** (8.0.17+) | `CREATE INDEX idx ON t((CAST(json_col->'$[*]' AS UNSIGNED ARRAY)))` | 🟢 P2 |
| **降序索引** (8.0+) | `CREATE INDEX idx ON t(col1 ASC, col2 DESC)` — 部分覆盖（有 ASC/DESC，但无混合） | 🟢 P2 |
| **前缀索引** | `CREATE INDEX idx ON t(col(10))` | 🟢 P2 |
| **不可见索引** (8.0+) | `CREATE INDEX idx ON t(col) INVISIBLE` | 🟢 P2 |
| **HASH 索引** (MEMORY) | `INDEX idx(col) USING HASH` | 🟢 P2 |

### 扩展建议

```cpp
// 在 create_index_stmt 中添加 FULLTEXT/SPATIAL 分支
if (schema::target_dbms == "mysql") {
    auto idx_type = d6();
    if (idx_type == 1) index_prefix = "fulltext ";
    else if (idx_type == 2) index_prefix = "spatial ";
}
```

---

## 四、CTE 语法

### 已覆盖

| CTE 特性 | 状态 | 说明 |
|---------|:----:|------|
| 基本 WITH 子句 | ✅ | `WITH cte AS (SELECT ...) SELECT ...` |
| 多个 CTE 项 | ✅ | `WITH cte1 AS (...), cte2 AS (...) SELECT ...` |
| CTE 内 DML | ❌ 关闭 | `has_data_mod_cte = false`（MySQL 不支持 RETURNING） |

### ❌ 缺失的 CTE 特性

| CTE 特性 | MySQL 8.4 语法 | 优先级 | 说明 |
|---------|---------------|:------:|------|
| **WITH RECURSIVE** | `WITH RECURSIVE cte(n) AS (SELECT 1 UNION ALL SELECT n+1 FROM cte WHERE n < 10) SELECT * FROM cte` | 🔴 P0 | MySQL 8.0+ 核心特性，递归查询 |
| **CTE 列名列表** | `WITH cte(col1, col2) AS (SELECT ...)` | 🟡 P1 | 显式列名指定 |
| **递归深度限制** | `SET SESSION cte_max_recursion_depth = N` | 🟢 P2 | 防止无限递归 |

### 扩展建议

WITH RECURSIVE 是 MySQL 8.0 的重要特性，应在 `common_table_expression` 构造函数中增加递归变体：

```cpp
// grammar.cc common_table_expression 构造函数
bool is_recursive = (schema::target_dbms == "mysql" && d6() > 3);
// 生成: WITH RECURSIVE cte AS (anchor UNION ALL recursive_part) SELECT ...
```

---

## 五、子查询

### 已覆盖

| 子查询类型 | 状态 | 说明 |
|-----------|:----:|------|
| 标量子查询 | ✅ | `(SELECT col FROM t LIMIT 1)` |
| EXISTS 子查询 | ✅ | `EXISTS (SELECT ...)` |
| IN 子查询 | ✅ | `expr IN (SELECT ...)` |
| 派生表 | ✅ | `(SELECT ...) AS alias` |
| LATERAL 派生表 | ✅ | `LATERAL (SELECT ...) AS alias` |
| 关联子查询 | ✅ | comp_subquery, in_query, exists_predicate |
| 行值子查询比较 | ✅ | `(expr1, expr2) = (SELECT ...)` |

### ❌ 缺失的子查询特性

| 子查询特性 | MySQL 8.4 语法 | 优先级 |
|-----------|---------------|:------:|
| **NOT IN 子查询** | `expr NOT IN (SELECT ...)` | 🟡 P1 |
| **量化比较** | `expr = ALL (SELECT ...)`, `expr > ANY (SELECT ...)` | 🟡 P1 |
| **JSON_TABLE** | `SELECT * FROM JSON_TABLE(json_col, '$[*]' COLUMNS(...)) AS jt` | 🟡 P1 |
| **TABLE 语句** (8.0.19+) | `TABLE t` 等价于 `SELECT * FROM t` | 🟢 P2 |
| **VALUES 语句** (8.0.19+) | `VALUES ROW(1,2), ROW(3,4)` | 🟢 P2 |

---

## 六、函数覆盖率

### 已覆盖的标量函数（~80 个注册项）

| 类别 | 函数列表 | 数量 |
|------|---------|:----:|
| **数学** | ABS, ACOS, ASIN, ATAN, ATAN2, CEILING, COS, COT, CRC32, DEGREES, EXP, FLOOR, LN, LOG, LOG2, LOG10, PI, POW, RADIANS, ROUND, SIGN, SIN, SQRT, TAN, TRUNCATE | 25 |
| **日期/时间** | ADDDATE, DATEDIFF, DAYOFMONTH, DAYOFWEEK, DAYOFYEAR, HOUR, MINUTE, MONTH, MONTHNAME, QUARTER, SECOND, SUBDATE, TIME_TO_SEC, TO_DAYS, TO_SECONDS, UNIX_TIMESTAMP, WEEK, WEEKDAY, WEEKOFYEAR, YEAR, YEARWEEK | 21 |
| **字符串** | ASCII, BIN, BIT_LENGTH, CHAR_LENGTH, CONCAT, CONCAT_WS, FIELD, FIND_IN_SET, FORMAT, HEX, INSTR, LCASE, LEFT, LENGTH, LOCATE, LOWER, LPAD, LTRIM, MD5, OCT, ORD, QUOTE, REPEAT, REPLACE, REVERSE, RIGHT, RPAD, RTRIM, SHA1, SHA2, SOUNDEX, SPACE, STRCMP, SUBSTRING, TO_BASE64, TRIM, UCASE, UPPER | 37 |
| **条件** | IF, IFNULL, ISNULL, NULLIF, GREATEST, LEAST, INTERVAL, MAKE_SET | 8 |
| **位运算** | BIT_COUNT | 1 |

### ❌ 缺失的函数类别

#### 6.1 JSON 函数（🔴 P0 — 整体缺失，~28 个函数）

MySQL 8.4 有完整的 JSON 函数族（[参考文档](https://dev.mysql.com/doc/refman/8.4/en/json-function-reference.html)）：

| 子类 | 缺失函数 |
|------|---------|
| **创建** | JSON_ARRAY, JSON_OBJECT, JSON_QUOTE |
| **搜索** | JSON_EXTRACT (->), JSON_UNQUOTE (->>), JSON_CONTAINS, JSON_CONTAINS_PATH, JSON_KEYS, JSON_SEARCH, MEMBER OF() |
| **修改** | JSON_SET, JSON_INSERT, JSON_REPLACE, JSON_REMOVE, JSON_ARRAY_APPEND, JSON_ARRAY_INSERT, JSON_MERGE_PATCH, JSON_MERGE_PRESERVE, JSON_UNQUOTE |
| **属性** | JSON_DEPTH, JSON_LENGTH, JSON_TYPE |
| **表函数** | JSON_TABLE |
| **工具** | JSON_PRETTY, JSON_STORAGE_FREE, JSON_STORAGE_SIZE, JSON_VALUE |
| **验证** | JSON_SCHEMA_VALID, JSON_SCHEMA_VALIDATION_REPORT |
| **其他** | JSON_OVERLAPS |

> **注意**：已注册 JSON_ARRAYAGG 和 JSON_OBJECTAGG（聚合函数），但标量 JSON 函数完全缺失。

#### 6.2 空间函数（🟡 P1 — 整体缺失，~60+ 个函数）

MySQL 8.4 空间函数族（[参考文档](https://dev.mysql.com/doc/refman/8.4/en/spatial-function-reference.html)）：

| 子类 | 缺失函数 |
|------|---------|
| **创建** | ST_GeomFromText, ST_PointFromText, ST_PolyFromText, Point, LineString, Polygon, GeomCollection |
| **关系** | ST_Contains, ST_Within, ST_Crosses, ST_Disjoint, ST_Equals, ST_Intersects, ST_Overlaps, ST_Touches |
| **距离** | ST_Distance, ST_Distance_Sphere |
| **运算** | ST_Buffer, ST_Difference, ST_Intersection, ST_SymDifference, ST_Union |
| **属性** | ST_SRID, ST_X, ST_Y, ST_IsEmpty, ST_IsValid, ST_Area, ST_Length |
| **MBR** | MBRContains, MBRWithin, MBREquals, MBRIntersects, MBRDisjoint, MBRCovers, MBRCoveredBy |

#### 6.3 全文搜索函数（🟡 P1 — 整体缺失）

| 函数 | MySQL 8.4 语法 |
|------|---------------|
| **MATCH() AGAINST()** | `MATCH(col) AGAINST('search' IN NATURAL LANGUAGE MODE)` |
| | `MATCH(col) AGAINST('+word -word' IN BOOLEAN MODE)` |
| | `MATCH(col) AGAINST('word' IN QUERY EXPANSION)` |

#### 6.4 信息函数（🟡 P1 — 整体缺失）

| 函数 | 说明 |
|------|------|
| VERSION() | MySQL 版本号 |
| DATABASE() / SCHEMA() | 当前数据库名 |
| USER() / CURRENT_USER() | 当前用户 |
| CONNECTION_ID() | 连接 ID |
| LAST_INSERT_ID() | 最后插入 ID |
| FOUND_ROWS() | 匹配行数 |
| ROW_COUNT() | 影响行数 |
| BENCHMARK(count, expr) | 性能测试 |
| CHARSET(str) | 字符集 |
| COLLATION(str) | 排序规则 |

#### 6.5 加密函数（🟢 P2 — 部分缺失）

| 函数 | 状态 |
|------|:----:|
| MD5 | ✅ |
| SHA1 | ✅ |
| SHA2 | ✅ |
| AES_ENCRYPT / AES_DECRYPT | ❌ |
| COMPRESS / UNCOMPRESS | ❌ |
| RANDOM_BYTES | ❌ |

#### 6.6 锁函数（🟢 P2 — 整体缺失）

| 函数 | 说明 |
|------|------|
| GET_LOCK(name, timeout) | 获取命名锁 |
| RELEASE_LOCK(name) | 释放命名锁 |
| IS_FREE_LOCK(name) | 检查锁是否空闲 |
| IS_USED_LOCK(name) | 检查锁是否被持有 |

#### 6.7 日期/时间函数（🟡 P1 — 部分缺失）

| 缺失函数 | 说明 |
|---------|------|
| NOW() / CURRENT_TIMESTAMP | 当前时间戳 |
| CURDATE() / CURRENT_DATE | 当前日期 |
| CURTIME() / CURRENT_TIME | 当前时间 |
| DATE_FORMAT(date, format) | 日期格式化 |
| STR_TO_DATE(str, format) | 字符串转日期 |
| DATE_ADD / DATE_SUB | 日期加减 INTERVAL |
| TIMESTAMPDIFF | 时间戳差 |
| TIMESTAMPADD | 时间戳加 |
| EXTRACT(unit FROM date) | 提取日期部分 |
| LAST_DAY(date) | 月最后一天 |
| MAKEDATE / MAKETIME | 构造日期/时间 |
| CONVERT_TZ | 时区转换 |
| UTC_DATE / UTC_TIME / UTC_TIMESTAMP | UTC 时间 |
| SYSDATE() | 系统时间 |

#### 6.8 字符串函数（🟢 P2 — 部分缺失）

| 缺失函数 | 说明 |
|---------|------|
| ELT(N, str1, str2, ...) | 返回第 N 个字符串 |
| EXPORT_SET | 位映射字符串 |
| INSERT(str, pos, len, newstr) | 字符串插入 |
| LOAD_FILE | 加载文件 |
| REGEXP_LIKE | REGEXP 别名 |
| REGEXP_REPLACE | 正则替换 |
| REGEXP_INSTR | 正则搜索位置 |
| REGEXP_SUBSTR | 正则提取子串 |

---

## 七、表达式覆盖率

### 已覆盖

| 表达式类型 | 状态 | 说明 |
|-----------|:----:|------|
| CASE WHEN...THEN...ELSE END | ✅ | case_expr |
| COALESCE | ✅ | coalesce |
| NULLIF | ✅ | nullif |
| 二元运算 (binop) | ✅ | 算术/位运算/比较/逻辑 |
| 函数调用 | ✅ | funcall |
| 窗口函数 | ✅ | win_funcall |
| 标量子查询 | ✅ | atomic_subselect |
| 列引用 | ✅ | column_reference |
| 常量 | ✅ | const_expr |
| LIKE / NOT LIKE | ✅ | like_op |
| BETWEEN | ✅ | between_op |
| IS NULL / IS NOT NULL | ✅ | null_predicate |
| IN (subquery) | ✅ | in_query |
| EXISTS | ✅ | exists_predicate |
| REGEXP / NOT REGEXP | ✅ | regexp_expr |
| SOUNDS LIKE | ✅ | sounds_like_expr |
| IS DISTINCT FROM | ✅ | distinct_pred |

### ❌ 缺失的表达式

| 表达式 | MySQL 8.4 语法 | 优先级 |
|--------|---------------|:------:|
| **CAST(expr AS type)** | `CAST(col AS UNSIGNED)`, `CAST(col AS CHAR)`, `CAST(col AS JSON)` | 🔴 P0 |
| **CONVERT(expr, type)** | `CONVERT(col, CHAR)`, `CONVERT(col USING utf8mb4)` | 🟡 P1 |
| **INTERVAL 表达式** | `date + INTERVAL 1 DAY`, `date - INTERVAL 2 MONTH` | 🟡 P1 |
| **COLLATE 表达式** | `col COLLATE utf8mb4_general_ci` | 🟢 P2 |
| **BINARY 强转** | `BINARY col`（二进制比较） | 🟢 P2 |
| **MATCH AGAINST** | `MATCH(col) AGAINST('text')` | 🟡 P1 |
| **VALUES(col)** | ON DUPLICATE KEY UPDATE 中引用插入值 | 🟢 P2 |
| **ROW() 构造器** | `ROW(1, 'a') = (SELECT ...)` — 已有 row_constructor | ✅ 已覆盖 |
| **位字符串字面量** | `b'1010'`, `0b1010` | 🟢 P2 |
| **十六进制字面量** | `0xFF`, `X'FF'` | 🟢 P2 |

---

## 八、操作符覆盖率

### 已覆盖（47 个 BINOP 注册项）

| 类别 | 操作符 | 类型组合数 |
|------|--------|:---------:|
| 位运算 | &, >>, <<, ^, \| | 5 |
| 比较 | >, <, >=, <=, =, <>, !=, <=> | 8 × 3 类型 = 24 |
| 算术 | +, -, *, /, %, DIV | 6 × 2 类型 = 12 |
| 逻辑 | &&, \|\|, XOR | 3 |
| **总计** | | **47** |

### ❌ 缺失的操作符/操作符变体

| 操作符 | MySQL 8.4 语法 | 优先级 |
|--------|---------------|:------:|
| **-> (JSON 提取)** | `json_col->'$.key'` 等价于 JSON_EXTRACT | 🔴 P0 |
| **->> (JSON 提取并解引)** | `json_col->>'$.key'` 等价于 JSON_UNQUOTE(JSON_EXTRACT) | 🔴 P0 |
| **MEMBER OF** | `value MEMBER OF(json_array)` | 🟡 P1 |
| **LIKE with ESCAPE** | `col LIKE 'pattern' ESCAPE '\'` | 🟢 P2 |
| **NOT REGEXP** | `col NOT REGEXP 'pattern'` — 已通过 regexp_expr 覆盖 | ✅ |
| **RLIKE** | REGEXP 别名 — 可作为 REGEXP 的别名添加 | 🟢 P2 |
| **MOD** | `a MOD b` 等价于 `a % b` | 🟢 P2 |
| **&& (字符串连接)** | MySQL 中 `\|\|` 默认为逻辑 OR，非字符串连接 | 🟢 P2 |
| **NOT LIKE** | `col NOT LIKE 'pattern'` — 已通过 like_op 覆盖 | ✅ |

### 复合运算符

| 运算符 | MySQL 8.4 | dbfuzz | 说明 |
|--------|:---------:|:------:|------|
| UNION DISTINCT | ✅ | ✅ | |
| UNION ALL | ✅ | ✅ | |
| INTERSECT | ✅ (8.0.31+) | ✅* | flag 默认关闭 |
| INTERSECT ALL | ❌ | ❌ | MySQL 不支持 |
| EXCEPT | ✅ (8.0.31+) | ✅* | flag 默认关闭 |
| EXCEPT ALL | ❌ | ❌ | MySQL 不支持 |

---

## 九、聚合函数覆盖率

### 已覆盖（31 个注册项）

| 函数 | 类型变体 | MySQL 8.4 |
|------|---------|:---------:|
| AVG | int→real, real→real | ✅ |
| BIT_AND | int→int | ✅ |
| BIT_OR | int→int | ✅ |
| BIT_XOR | int→int | ✅ |
| COUNT | *, int, real, text | ✅ |
| MAX | int, real | ✅ |
| MIN | int, real | ✅ |
| STDDEV_POP | int→real, real→real | ✅ |
| STDDEV_SAMP | int→real, real→real | ✅ |
| SUM | int→int, real→real | ✅ |
| VAR_POP | int→real, real→real | ✅ |
| VAR_SAMP | int→real, real→real | ✅ |
| ANY_VALUE | int, real, text | ✅ |
| GROUP_CONCAT | text→text, int→text | ✅ |
| JSON_ARRAYAGG | int→text, text→text | ✅ |
| JSON_OBJECTAGG | text→text | ✅ |

### ❌ 缺失的聚合函数

| 函数 | MySQL 8.4 说明 | 优先级 |
|------|---------------|:------:|
| COUNT(DISTINCT expr) | 去重计数 | 🟡 P1 |
| GROUP_CONCAT(DISTINCT) | 去重分组连接 | 🟢 P2 |
| GROUP_CONCAT(ORDER BY) | 排序分组连接 | 🟢 P2 |
| GROUP_CONCAT(SEPARATOR) | 自定义分隔符 | 🟢 P2 |

---

## 十、窗口函数覆盖率

### 已覆盖（15 个注册项）

| 函数 | 类别 | MySQL 8.4 |
|------|------|:---------:|
| ROW_NUMBER | 排名 | ✅ |
| RANK | 排名 | ✅ |
| DENSE_RANK | 排名 | ✅ |
| PERCENT_RANK | 排名 | ✅ |
| CUME_DIST | 排名 | ✅ |
| NTILE | 分布 | ✅ |
| FIRST_VALUE | 值 | ✅ |
| LAST_VALUE | 值 | ✅ |
| LAG | 偏移 | ✅ |
| LEAD | 偏移 | ✅ |

### ❌ 缺失的窗口函数

| 函数 | MySQL 8.4 说明 | 优先级 |
|------|---------------|:------:|
| **NTH_VALUE(expr, N)** | 返回第 N 行的值 | 🟡 P1 |

### ❌ 缺失的窗口特性

| 特性 | MySQL 8.4 语法 | 优先级 |
|------|---------------|:------:|
| **Window Frame** | `ROWS BETWEEN 1 PRECEDING AND 1 FOLLOWING` | 🔴 P0 |
| **RANGE Frame** | `RANGE BETWEEN INTERVAL 1 DAY PRECEDING AND CURRENT ROW` | 🟡 P1 |
| **GROUPS Frame** | `GROUPS BETWEEN 1 PRECEDING AND 1 FOLLOWING` | 🟢 P2 |
| **命名窗口** | `WINDOW w AS (PARTITION BY col ORDER BY col2)` — 已有支持 | ✅ |
| **NULL 处理** | `LAST_VALUE(col) IGNORE NULLS` (MySQL 8.0.28+ 不支持) | 🟢 P2 |

> **注意**：`has_window_frame = false` 导致 MySQL 不生成窗口帧子句。MySQL 8.0+ 完整支持 ROWS/RANGE/GROUPS 帧。

---

## 十一、JOIN 类型覆盖率

| JOIN 类型 | MySQL 8.4 | dbfuzz | 说明 |
|-----------|:---------:|:------:|------|
| INNER JOIN | ✅ | ✅ | |
| LEFT [OUTER] JOIN | ✅ | ✅ | |
| RIGHT [OUTER] JOIN | ✅ | ✅ | |
| CROSS JOIN | ✅ | ✅ | |
| STRAIGHT_JOIN | ✅ | ✅ | MySQL 特有 |
| **NATURAL JOIN** | ✅ | ❌ | 基于同名列自动连接 |
| **NATURAL LEFT JOIN** | ✅ | ❌ | |

---

## 十二、语句覆盖率

### 已覆盖的语句（~22 种）

| 类别 | 语句 |
|------|------|
| **DML** | SELECT, INSERT, INSERT SELECT, UPDATE, DELETE, REPLACE, UPSERT |
| **查询** | UNION/INTERSECT/EXCEPT, CTE, SELECT FOR UPDATE |
| **DDL** | CREATE TABLE, CREATE TABLE SELECT, CREATE VIEW, CREATE INDEX, CREATE TRIGGER, ALTER TABLE, DROP TABLE |
| **辅助** | EXPLAIN, 表维护 (CHECKSUM/CHECK/OPTIMIZE/REPAIR), SET, DO |

### ❌ 缺失的语句

| 语句 | MySQL 8.4 说明 | 优先级 |
|------|---------------|:------:|
| **SAVEPOINT / RELEASE SAVEPOINT** | 事务内保存点 | 🟡 P1 |
| **LOCK TABLES / UNLOCK TABLES** | 表级锁 | 🟡 P1 |
| **HANDLER** | 直接表访问接口 | 🟢 P2 |
| **LOAD DATA INFILE** | 批量数据加载 | 🟢 P2 |
| **SELECT INTO OUTFILE** | 查询结果导出 | 🟢 P2 |
| **CREATE/ALTER/DROP PROCEDURE** | 存储过程管理 | 🟢 P2 |
| **CREATE/ALTER/DROP FUNCTION** | 自定义函数管理 | 🟢 P2 |
| **CREATE/ALTER/DROP EVENT** | 事件调度 | 🟢 P2 |
| **GRANT / REVOKE** | 权限管理 | 🟢 P2 |
| **RENAME TABLE** | 表重命名（独立语句） | 🟢 P2 |
| **TRUNCATE TABLE** | 清空表 | 🟢 P2 |
| **FLUSH / RESET** | 服务器管理 | 🟢 P2 |
| **SHOW** | 信息查询 (SHOW TABLES, SHOW STATUS 等) | 🟢 P2 |
| **DESCRIBE / DESC** | 表结构查看 | 🟢 P2 |

---

## 十三、ALTER TABLE 操作覆盖率

### 已覆盖

| 操作 | 状态 |
|------|:----:|
| RENAME TABLE | ✅ |
| RENAME COLUMN | ✅ |
| ADD COLUMN | ✅ |

### ❌ 缺失的操作

| 操作 | MySQL 8.4 语法 | 优先级 |
|------|---------------|:------:|
| **DROP COLUMN** | `ALTER TABLE t DROP COLUMN col` | 🟡 P1 |
| **MODIFY COLUMN** | `ALTER TABLE t MODIFY COLUMN col new_type` | 🟡 P1 |
| **CHANGE COLUMN** | `ALTER TABLE t CHANGE old_col new_col new_type` | 🟢 P2 |
| **ADD INDEX** | `ALTER TABLE t ADD INDEX idx(col)` | 🟡 P1 |
| **DROP INDEX** | `ALTER TABLE t DROP INDEX idx` | 🟢 P2 |
| **ADD PRIMARY KEY** | `ALTER TABLE t ADD PRIMARY KEY(col)` | 🟢 P2 |
| **ADD FOREIGN KEY** | `ALTER TABLE t ADD FOREIGN KEY(col) REFERENCES t2(col2)` | 🟢 P2 |
| **CONVERT TO CHARSET** | `ALTER TABLE t CONVERT TO CHARACTER SET utf8mb4` | 🟢 P2 |
| **ENGINE 变更** | `ALTER TABLE t ENGINE = InnoDB` | 🟢 P2 |

---

## 十四、事务特性覆盖率

### 已覆盖

| 特性 | 状态 |
|------|:----:|
| START TRANSACTION | ✅ |
| COMMIT | ✅ |
| ROLLBACK | ✅ |
| 隔离级别设置 | ✅ (REPEATABLE READ, 连接时设置) |

### ❌ 缺失的事务特性

| 特性 | MySQL 8.4 语法 | 优先级 |
|------|---------------|:------:|
| **SAVEPOINT** | `SAVEPOINT sp_name` | 🟡 P1 |
| **RELEASE SAVEPOINT** | `RELEASE SAVEPOINT sp_name` | 🟡 P1 |
| **ROLLBACK TO SAVEPOINT** | `ROLLBACK TO SAVEPOINT sp_name` | 🟡 P1 |
| **SET TRANSACTION** | `SET TRANSACTION ISOLATION LEVEL READ COMMITTED` | 🟡 P1 |
| **LOCK IN SHARE MODE** | `SELECT ... LOCK IN SHARE MODE` | 🟢 P2 |
| **FOR SHARE** (8.0+) | `SELECT ... FOR SHARE` | 🟢 P2 |
| **XA 事务** | `XA START/END/PREPARE/COMMIT/ROLLBACK` | 🟢 P2 |

---

## 十五、SET 参数覆盖率

### 已覆盖（18 个 optimizer_switch 选项）

batch_key_access, block_nested_loop, condition_fanout_filter, derived_merge, duplicateweedout, firstmatch, index_condition_pushdown, index_merge, index_merge_intersection, index_merge_union, index_merge_sort_union, loosescan, materialization, mrr, mrr_cost_based, semijoin, subquery_materialization_cost_based, use_index_extensions

### ❌ 缺失的 SET 参数

| 参数 | MySQL 8.4 说明 | 优先级 |
|------|---------------|:------:|
| **sql_mode** | ONLY_FULL_GROUP_BY, STRICT_TRANS_TABLES 等 | 🟡 P1 |
| **innodb_lock_wait_timeout** | InnoDB 锁等待超时 | 🟢 P2 |
| **max_execution_time** | 查询最大执行时间 | 🟢 P2 |
| **sort_buffer_size** | 排序缓冲区大小 | 🟢 P2 |
| **join_buffer_size** | 连接缓冲区大小 | 🟢 P2 |
| **tmp_table_size** | 临时表大小 | 🟢 P2 |
| **eq_range_index_diligence_limit** | 等值范围索引限制 | 🟢 P2 |
| **cte_max_recursion_depth** | CTE 递归深度限制 | 🟢 P2 |

---

## 十六、总覆盖率统计

| 维度 | MySQL 8.4 总数 | dbfuzz 已覆盖 | 覆盖率 | 缺失数 |
|------|:-------------:|:------------:|:-----:|:-----:|
| 数据类型 | 12 大类 | 5 规范 + 20 别名 | ~42% | 7 类 |
| 存储引擎 | 5 种常用 | 0（未填充） | 0% | 5 |
| 索引类型 | 7 种 | 2 (B-Tree + UNIQUE) | 29% | 5 |
| CTE | 3 种 (基本/递归/列名) | 1 (基本) | 33% | 2 |
| 子查询 | 7 种 | 6 | 86% | 1 |
| 标量函数 | ~150+ | ~80 | ~53% | ~70 |
| JSON 函数 | ~28 | 0 (仅聚合 2) | 7% | ~26 |
| 空间函数 | ~60+ | 0 | 0% | ~60 |
| 全文搜索 | 3 (MATCH 模式) | 0 | 0% | 3 |
| 信息函数 | ~12 | 0 | 0% | ~12 |
| 聚合函数 | ~18 | 16 | 89% | 2 |
| 窗口函数 | 11 | 10 | 91% | 1 |
| 窗口特性 | 4 (帧/NULL/命名) | 1 (命名) | 25% | 3 |
| 操作符 | ~15 | 13 | 87% | 2 |
| JOIN 类型 | 7 | 5 | 71% | 2 |
| 复合运算符 | 4 | 4 | 100% | 0 |
| 语句类型 | ~35 | ~22 | 63% | ~13 |
| ALTER TABLE 操作 | ~10 | 3 | 30% | 7 |
| 事务特性 | 7 | 3 | 43% | 4 |

---

## 十七、扩展优先级建议

### 🔴 P0 — 必须补齐（高 Bug 发现潜力）

| 项目 | 说明 | 预期工作量 |
|------|------|:---------:|
| **CAST 表达式** | 类型转换是许多高级特性的基础 | 2h |
| **JSON 数据类型 + 核心函数** | JSON_EXTRACT, JSON_SET, JSON_CONTAINS, ->> 等 | 4h |
| **WITH RECURSIVE** | MySQL 8.0 核心 CTE 特性 | 3h |
| **窗口帧 (Window Frame)** | ROWS/RANGE BETWEEN...AND... | 2h |
| **存储引擎 + 表选项** | ENGINE=InnoDB/MyISAM, CHARSET 等 | 1h |

### 🟡 P1 — 应该补齐（中等 Bug 发现潜力）

| 项目 | 说明 | 预期工作量 |
|------|------|:---------:|
| **FULLTEXT 索引 + MATCH AGAINST** | 全文搜索是 MySQL 重要特性 | 3h |
| **空间数据类型 + 核心函数** | ST_GeomFromText, ST_Contains, ST_Distance | 4h |
| **INTERVAL 日期算术** | date + INTERVAL N DAY | 2h |
| **日期/时间函数补齐** | NOW, CURDATE, DATE_FORMAT, EXTRACT 等 | 2h |
| **信息函数** | VERSION, DATABASE, USER 等 | 1h |
| **SAVEPOINT 事务** | SAVEPOINT/RELEASE/ROLLBACK TO | 2h |
| **ALTER TABLE 补齐** | DROP/MODIFY COLUMN, ADD INDEX | 2h |
| **NTH_VALUE 窗口函数** | 唯一缺失的窗口函数 | 0.5h |
| **sql_mode SET 参数** | ONLY_FULL_GROUP_BY 等 | 1h |
| **NOT IN 子查询** | 补充 in_query 的 NOT 变体 | 1h |
| **MEMBER OF 表达式** | JSON 数组成员测试 | 1h |

### 🟢 P2 — 可选补齐（低优先级）

| 项目 | 说明 |
|------|------|
| BLOB/BINARY 数据类型 | 二进制数据测试 |
| BIT 数据类型 | 位字段 |
| LOCK TABLES | 表级锁 |
| HANDLER 语句 | 直接表访问 |
| 加密函数 (AES_ENCRYPT 等) | 加密场景 |
| 锁函数 (GET_LOCK 等) | 命名锁场景 |
| XA 事务 | 分布式事务 |
| 正则函数 (REGEXP_REPLACE 等) | MySQL 8.0.4+ |
| SPATIAL 索引 | 空间索引 |
| 函数索引 | 表达式索引 |
| CONVERT 表达式 | 字符集转换 |

---

## 参考文档

- [MySQL 8.4 SQL Statements](https://dev.mysql.com/doc/refman/8.4/en/sql-statements.html)
- [MySQL 8.4 Built-In Function Reference](https://dev.mysql.com/doc/refman/8.4/en/built-in-function-reference.html)
- [MySQL 8.4 JSON Functions](https://dev.mysql.com/doc/refman/8.4/en/json-function-reference.html)
- [MySQL 8.4 Spatial Functions](https://dev.mysql.com/doc/refman/8.4/en/spatial-function-reference.html)
- [MySQL 8.4 Window Functions](https://dev.mysql.com/doc/refman/8.4/en/window-function-descriptions.html)
- [MySQL 8.4 Data Types](https://dev.mysql.com/doc/refman/8.4/en/data-types.html)
- [MySQL 8.4 Storage Engines](https://dev.mysql.com/doc/refman/8.4/en/storage-engines.html)
- [MySQL 8.4 Full-Text Search](https://dev.mysql.com/doc/refman/8.4/en/fulltext-search.html)
