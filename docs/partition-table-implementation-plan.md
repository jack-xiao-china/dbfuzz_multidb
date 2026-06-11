# 分区表补齐方案 — MySQL & PostgreSQL

## 1. 背景与目标

当前 dbfuzz 仅 CockroachDB 有有限的分区表支持（LIST / RANGE），MySQL 和 PostgreSQL 均**完全缺失**分区表建表语法、二级分区、分区管理语句。

本方案参照以下官方文档，补齐 MySQL 8.4 和 PostgreSQL 16 的分区表全量支持：

- [MySQL 8.4 — Chapter 26: Partitioning](https://dev.mysql.com/doc/refman/8.4/en/partitioning-overview.html)
- [PostgreSQL 16 — 5.12. Table Partitioning](https://www.postgresql.org/docs/16/ddl-partitioning.html)

### 目标覆盖率

| 维度 | MySQL 当前 | MySQL 目标 | PG 当前 | PG 目标 |
|------|:---:|:---:|:---:|:---:|
| 建表分区语法 | 0% | 100% | 0% | 100% |
| 二级分区 | 0% | 100% | 0% | 100% |
| 分区管理 ALTER TABLE | 0% | 80% | 0% | 100% |
| 分区选择查询 | 0% | 100% | N/A | N/A |

---

## 2. MySQL 8.4 分区表特性清单

### 2.1 主分区类型（PARTITION BY）

| 类型 | 语法 | 说明 |
|------|------|------|
| **RANGE** | `PARTITION BY RANGE(col) (PARTITION p0 VALUES LESS THAN (v), ...)` | 按列值范围分区 |
| **LIST** | `PARTITION BY LIST(col) (PARTITION p0 VALUES IN (v1,v2), ...)` | 按离散值列表分区 |
| **HASH** | `PARTITION BY HASH(expr) PARTITIONS n` | 按表达式哈希分区 |
| **KEY** | `PARTITION BY KEY(col_list) PARTITIONS n` | 按 MySQL 内置哈希函数分区 |
| **LINEAR HASH** | `PARTITION BY LINEAR HASH(expr) PARTITIONS n` | 2 的幂线性哈希 |
| **LINEAR KEY** | `PARTITION BY LINEAR KEY(col_list) PARTITIONS n` | 2 的幂线性 key |

### 2.2 RANGE COLUMNS / LIST COLUMNS

| 类型 | 语法 | 说明 |
|------|------|------|
| **RANGE COLUMNS** | `PARTITION BY RANGE COLUMNS(col) (...)` | 直接按列值（支持非整数、多列） |
| **LIST COLUMNS** | `PARTITION BY LIST COLUMNS(col) (...)` | 直接按列值列表（支持非整数、多列） |

### 2.3 二级分区（SUBPARTITION）

- 仅 RANGE 和 LIST 分区可设子分区
- 子分区类型：HASH 或 KEY
- 语法：`SUBPARTITION BY HASH(expr) SUBPARTITIONS n` 或显式列出 `SUBPARTITION s0, s1, ...`

### 2.4 分区管理语句（ALTER TABLE）

| 操作 | 语法 | 适用类型 |
|------|------|----------|
| ADD PARTITION | `ALTER TABLE t ADD PARTITION (PARTITION p4 VALUES LESS THAN (2030))` | RANGE, LIST |
| DROP PARTITION | `ALTER TABLE t DROP PARTITION p0` | RANGE, LIST |
| TRUNCATE PARTITION | `ALTER TABLE t TRUNCATE PARTITION p0` | ALL |
| REORGANIZE PARTITION | `ALTER TABLE t REORGANIZE PARTITION p0 INTO (PARTITION p0a ..., PARTITION p0b ...)` | RANGE, LIST |
| COALESCE PARTITION | `ALTER TABLE t COALESCE PARTITION 2` | HASH, KEY |
| ANALYZE PARTITION | `ALTER TABLE t ANALYZE PARTITION p0` | ALL |
| CHECK PARTITION | `ALTER TABLE t CHECK PARTITION p0` | ALL |
| OPTIMIZE PARTITION | `ALTER TABLE t OPTIMIZE PARTITION p0` | ALL |
| REBUILD PARTITION | `ALTER TABLE t REBUILD PARTITION p0` | ALL |
| REPAIR PARTITION | `ALTER TABLE t REPAIR PARTITION p0` | ALL |
| REMOVE PARTITIONING | `ALTER TABLE t REMOVE PARTITIONING` | ALL |

### 2.5 分区选择（Partition Selection）

```sql
SELECT * FROM t PARTITION (p0, p1);
DELETE FROM t PARTITION (p2) WHERE ...;
UPDATE t PARTITION (p0) SET ... WHERE ...;
INSERT INTO t PARTITION (p0) VALUES (...);
REPLACE INTO t PARTITION (p0) VALUES (...);
```

### 2.6 关键限制（必须处理）

1. **分区键必须包含在所有唯一键/主键中** — 如果表有 PRIMARY KEY(id)，分区键 col 必须加入主键：`PRIMARY KEY(id, col)`
2. HASH/KEY 分区表达式必须返回整数
3. 分区列不能是子查询
4. 不支持外键约束
5. 最大分区数：8192（实际通常限制为 1024）
6. RANGE/LIST 的分区值必须单调递增

---

## 3. PostgreSQL 16 分区表特性清单

### 3.1 主分区类型（PARTITION BY）

| 类型 | 语法 | 说明 |
|------|------|------|
| **RANGE** | `PARTITION BY RANGE (col)` | 按范围分区，支持多列 |
| **LIST** | `PARTITION BY LIST (col)` | 按离散值列表分区 |
| **HASH** | `PARTITION BY HASH (col)` | 按哈希分区（PG 11+） |

### 3.2 分区创建（CREATE TABLE ... PARTITION OF）

```sql
-- RANGE 子分区
CREATE TABLE orders_2024 PARTITION OF orders
    FOR VALUES FROM ('2024-01-01') TO ('2025-01-01');

-- LIST 子分区
CREATE TABLE events_us PARTITION OF events
    FOR VALUES IN ('US', 'CA');

-- HASH 子分区
CREATE TABLE sessions_p0 PARTITION OF sessions
    FOR VALUES WITH (MODULUS 4, REMAINDER 0);

-- DEFAULT 分区（捕获不匹配任何分区的行）
CREATE TABLE orders_default PARTITION OF orders DEFAULT;
```

### 3.3 二级分区（Subpartitioning）

```sql
-- 子分区本身也是分区表
CREATE TABLE orders_2024 PARTITION OF orders
    FOR VALUES FROM ('2024-01-01') TO ('2025-01-01')
    PARTITION BY LIST (region);

CREATE TABLE orders_2024_us PARTITION OF orders_2024
    FOR VALUES IN ('US');
```

### 3.4 分区管理语句（ALTER TABLE）

| 操作 | 语法 |
|------|------|
| ATTACH PARTITION | `ALTER TABLE parent ATTACH PARTITION child FOR VALUES FROM (...) TO (...)` |
| DETACH PARTITION | `ALTER TABLE parent DETACH PARTITION child` |
| DETACH CONCURRENTLY | `ALTER TABLE parent DETACH PARTITION child CONCURRENTLY` (PG 14+) |
| DETACH FINALIZE | `ALTER TABLE parent DETACH PARTITION child FINALIZE` (PG 14+) |

### 3.5 关键限制（必须处理）

1. 分区父表不能有行数据（数据在子分区中）
2. HASH 分区使用 MODULUS / REMAINDER 语法
3. RANGE 支持多列分区：`FOR VALUES FROM (v1, v2) TO (v3, v4)`
4. DEFAULT 分区捕获不匹配的行
5. ATTACH 时验证子表结构与父表一致
6. 分区表可以有 UNIQUE 约束，但必须包含分区键

---

## 4. 实施方案

### 4.1 新增 Feature Flags

在 `schema.hh` 的 `db_feature_flags` 中新增：

```cpp
// Partition table support
bool has_partition_table    = false;  // 支持分区表建表
bool has_subpartition       = false;  // 支持二级分区
bool has_partition_mgmt     = false;  // 支持 ALTER TABLE 分区管理
bool has_partition_select   = false;  // 支持 SELECT ... PARTITION (p0)
bool has_partition_default  = false;  // 支持 DEFAULT 分区 (PG)
bool has_attach_partition   = false;  // 支持 ATTACH/DETACH PARTITION (PG)
```

启用情况：

| Flag | MySQL | PostgreSQL | CockroachDB |
|------|:---:|:---:|:---:|
| `has_partition_table` | ✅ | ✅ | ✅ (已有) |
| `has_subpartition` | ✅ | ✅ | ❌ |
| `has_partition_mgmt` | ✅ | ❌ | ❌ |
| `has_partition_select` | ✅ | ❌ | ❌ |
| `has_partition_default` | ❌ | ✅ | ❌ |
| `has_attach_partition` | ❌ | ✅ | ❌ |

### 4.2 整体架构：分区选项生成函数

遵循 CockroachDB 的 `cockroach_table_option()` 模式，新增两个生成函数：

```cpp
// grammar.cc 中新增
string mysql_partition_option(prod* p, shared_ptr<table> created_table, int primary_col_id);
string pg_partition_option(prod* p, shared_ptr<table> created_table, int primary_col_id);
```

### 4.3 Sprint 划分

---

#### Sprint 1：MySQL 分区建表语法（P0）

**目标**：支持 MySQL 6 种分区类型 + RANGE COLUMNS / LIST COLUMNS

**修改文件**：`src/grammar/grammar.cc`

**新增函数** `mysql_partition_option()`：

```
随机选择分区类型 (d9):
  1-2 → RANGE (col)
    生成: PARTITION BY RANGE (col) (
      PARTITION p0 VALUES LESS THAN (v1),
      PARTITION p1 VALUES LESS THAN (v2),
      ...
      PARTITION pN VALUES LESS THAN MAXVALUE
    )
  
  3-4 → LIST (col)
    生成: PARTITION BY LIST (col) (
      PARTITION p0 VALUES IN (v1, v2, ...),
      PARTITION p1 VALUES IN (v3, v4, ...),
      ...
    )
  
  5 → HASH (col) PARTITIONS n
  
  6 → KEY (col) PARTITIONS n
  
  7 → RANGE COLUMNS (col) -- 类似 RANGE，但用 COLUMNS 语法
  8 → LIST COLUMNS (col)  -- 类似 LIST，但用 COLUMNS 语法
  9 → LINEAR HASH / LINEAR KEY
```

**关键约束处理**：
- 分区列选择：优先选择主键列（满足"分区键必须在主键中"约束）
- 如果选了非主键列，则需将主键改为复合主键 `PRIMARY KEY(pk_col, partition_col)`
- RANGE 分区值必须单调递增：使用 `start + dx(step)` 递增
- 最后一个分区使用 `MAXVALUE`
- 分区数限制：`dx(4)` 即 1-4 个分区
- HASH/KEY 的 PARTITIONS n：`dx(4)` 即 1-4

**修改 `create_table_stmt` 构造函数**：

在现有的 CockroachDB/Yugabyte 分支后追加 MySQL 分支：

```cpp
else if (scope->schema->target_dbms == "mysql" && has_primary_key 
         && scope->schema->features.has_partition_table && d6() <= 3) {
    has_option = true;
    table_option += " " + mysql_partition_option(this, created_table, primary_col_id);
}
```

**修改 `create_table_stmt::out()`**：
- MySQL 分区表：`PARTITION BY ...` 必须在 `ENGINE = ...` 之前
- 如果使用了非主键列分区，需要修改主键输出为复合主键

**涉及文件**：

| 文件 | 操作 |
|------|------|
| `src/grammar/grammar.cc` | 修改 — 新增 `mysql_partition_option()` + 修改 `create_table_stmt` |
| `src/schema/mysql.cc` | 修改 — 启用 feature flags |
| `src/schema/schema.hh` | 修改 — 新增 feature flags |

**验证**：dry-run MySQL 100 queries，确认 PARTITION BY RANGE/LIST/HASH/KEY 被生成

---

#### Sprint 2：MySQL 二级分区（P1）

**目标**：支持 RANGE+HASH / RANGE+KEY / LIST+HASH / LIST+KEY 子分区

**修改文件**：`src/grammar/grammar.cc`

在 `mysql_partition_option()` 中，当主分区为 RANGE 或 LIST 时，30% 概率追加子分区：

```sql
-- RANGE + HASH 子分区
PARTITION BY RANGE (col1)
SUBPARTITION BY HASH (col2)
SUBPARTITIONS 3 (
    PARTITION p0 VALUES LESS THAN (100),
    PARTITION p1 VALUES LESS THAN (200),
    PARTITION p2 VALUES LESS THAN MAXVALUE
)
```

或显式列出子分区：

```sql
PARTITION BY RANGE (col1)
SUBPARTITION BY KEY (col2) (
    PARTITION p0 VALUES LESS THAN (100) (
        SUBPARTITION s0,
        SUBPARTITION s1
    ),
    PARTITION p1 VALUES LESS THAN (200) (
        SUBPARTITION s2,
        SUBPARTITION s3
    )
)
```

**关键约束**：
- 子分区列必须是整数类型（HASH）或可哈希类型（KEY）
- 子分区数：2-4 个
- 子分区列 ≠ 主分区列

**验证**：dry-run 确认 SUBPARTITION BY 被生成

---

#### Sprint 3：MySQL 分区管理 + 分区选择（P1）

**目标**：ALTER TABLE 分区管理语句 + SELECT/UPDATE/DELETE 分区选择

**3.1 分区管理 — 扩展 `alter_table_stmt`**

在 `grammar.hh` 的 `alter_table_stmt` 中新增 stmt_type：
- 6: ADD PARTITION
- 7: DROP PARTITION
- 8: TRUNCATE PARTITION
- 9: COALESCE PARTITION
- 10: REORGANIZE PARTITION
- 11: ANALYZE/CHECK/OPTIMIZE/REBUILD/REPAIR PARTITION
- 12: REMOVE PARTITIONING

**需要追踪分区表信息**：

由于 dbfuzz 是随机生成，ALTER TABLE 操作分区表需要知道：
- 哪些表是分区表
- 分区表的分区类型和分区名

**方案**：在全局 map 中记录分区表信息（类似 `tabl_pk_col_id` 的模式）：

```cpp
// 新增全局变量
struct partition_info {
    string partition_type;           // "RANGE", "LIST", "HASH", "KEY"
    vector<string> partition_names;  // ["p0", "p1", "p2"]
    string partition_col;            // 分区列名
    bool has_subpartition;
};
extern map<string, partition_info> table_partitions;
```

**生成的语句示例**：

```sql
-- ADD PARTITION (RANGE)
ALTER TABLE t ADD PARTITION (PARTITION p4 VALUES LESS THAN (300))

-- DROP PARTITION
ALTER TABLE t DROP PARTITION p0

-- TRUNCATE PARTITION
ALTER TABLE t TRUNCATE PARTITION p0

-- COALESCE PARTITION (HASH/KEY only)
ALTER TABLE t COALESCE PARTITION 1

-- REORGANIZE PARTITION
ALTER TABLE t REORGANIZE PARTITION p0 INTO (
    PARTITION p0a VALUES LESS THAN (50),
    PARTITION p0b VALUES LESS THAN (100)
)

-- ANALYZE/CHECK/OPTIMIZE/REBUILD/REPAIR
ALTER TABLE t ANALYZE PARTITION p0

-- REMOVE PARTITIONING
ALTER TABLE t REMOVE PARTITIONING
```

**3.2 分区选择 — 修改 SELECT/UPDATE/DELETE/INSERT**

在 `table_or_query_name::out()` 中，MySQL 场景下，如果目标表是分区表，20% 概率追加 `PARTITION (p0, p1)` 子句。

修改 `table_or_query_name` 结构：

```cpp
struct table_or_query_name : table_ref {
    string partition_hint;  // MySQL: PARTITION (p0, p1)
};
```

**涉及文件**：

| 文件 | 操作 |
|------|------|
| `src/grammar/grammar.hh` | 修改 — alter_table_stmt 扩展 + partition_info 结构 |
| `src/grammar/grammar.cc` | 修改 — alter_table_stmt 构造 + 分区选择 |
| `src/schema/mysql.cc` | 修改 — 启用 has_partition_mgmt, has_partition_select |

**验证**：dry-run 确认 ALTER TABLE ... ADD/DROP/TRUNCATE PARTITION 和 SELECT ... PARTITION(p0) 被生成

---

#### Sprint 4：PostgreSQL 分区建表语法（P0）

**目标**：支持 PG 三种分区类型 + 二级分区 + DEFAULT 分区

**新增函数** `pg_partition_option()`：

```
随机选择分区类型 (d6):
  1-2 → RANGE (col)
    生成: PARTITION BY RANGE (col)
    + 自动创建 2-4 个子分区: CREATE TABLE xxx_p0 PARTITION OF xxx FOR VALUES FROM (...) TO (...)
    
  3-4 → LIST (col)
    生成: PARTITION BY LIST (col)
    + 自动创建 2-3 个子分区: CREATE TABLE xxx_p0 PARTITION OF xxx FOR VALUES IN (...)
    
  5-6 → HASH (col)
    生成: PARTITION BY HASH (col)
    + 自动创建 2-4 个子分区: CREATE TABLE xxx_p0 PARTITION OF xxx FOR VALUES WITH (MODULUS n, REMAINDER r)
```

**PG 与 MySQL 的关键差异**：
1. PG 使用 `CREATE TABLE ... PARTITION OF parent` 创建子分区（独立语句），而非嵌入建表语句中
2. PG 支持 DEFAULT 分区
3. PG 的 RANGE 支持多列：`FOR VALUES FROM (v1, v2) TO (v3, v4)`
4. PG 的 HASH 使用 `MODULUS n, REMAINDER r` 语法

**实现方案**：

由于 PG 的子分区是独立的 CREATE TABLE 语句，需要修改 `create_table_stmt` 以支持**输出多条语句**：

```cpp
struct create_table_stmt: prod {
    // ... existing members ...
    vector<string> partition_subtable_stmts;  // PG: 子分区创建语句
};
```

在 `create_table_stmt::out()` 中，如果是 PG 分区表：

```cpp
void create_table_stmt::out(std::ostream &out) {
    // 1. 输出主表 CREATE TABLE ... PARTITION BY RANGE (col)
    out << "create table " << created_table->name << " ( ... ) " << table_option;
    
    // 2. 输出子分区语句 (PG)
    for (auto &stmt : partition_subtable_stmts) {
        out << ";\n" << stmt;
    }
}
```

**子分区生成示例**：

```sql
-- RANGE
CREATE TABLE t_p0 PARTITION OF t FOR VALUES FROM (0) TO (50);
CREATE TABLE t_p1 PARTITION OF t FOR VALUES FROM (50) TO (100);
CREATE TABLE t_default PARTITION OF t DEFAULT;

-- LIST
CREATE TABLE t_p0 PARTITION OF t FOR VALUES IN ('A', 'B');
CREATE TABLE t_p1 PARTITION OF t FOR VALUES IN ('C', 'D');
CREATE TABLE t_default PARTITION OF t DEFAULT;

-- HASH
CREATE TABLE t_p0 PARTITION OF t FOR VALUES WITH (MODULUS 3, REMAINDER 0);
CREATE TABLE t_p1 PARTITION OF t FOR VALUES WITH (MODULUS 3, REMAINDER 1);
CREATE TABLE t_p2 PARTITION OF t FOR VALUES WITH (MODULUS 3, REMAINDER 2);
```

**二级分区**：

```sql
-- 一级分区本身也是分区表
CREATE TABLE t_2024 PARTITION OF t
    FOR VALUES FROM ('2024-01-01') TO ('2025-01-01')
    PARTITION BY LIST (region);

CREATE TABLE t_2024_us PARTITION OF t_2024
    FOR VALUES IN ('US');
```

30% 概率生成二级分区。

**涉及文件**：

| 文件 | 操作 |
|------|------|
| `src/grammar/grammar.cc` | 修改 — 新增 `pg_partition_option()` + 修改 `create_table_stmt` |
| `src/grammar/grammar.hh` | 修改 — create_table_stmt 新增 partition_subtable_stmts |
| `src/schema/postgres.cc` | 修改 — 启用 feature flags |

**验证**：dry-run PG 100 queries，确认 PARTITION BY + PARTITION OF 被生成

---

#### Sprint 5：PostgreSQL 分区管理 + CockroachDB 补齐（P1）

**目标**：PG ATTACH/DETACH PARTITION + CockroachDB HASH 分区

**5.1 PostgreSQL ATTACH / DETACH**

扩展 `alter_table_stmt`，新增 stmt_type：
- 13: ATTACH PARTITION
- 14: DETACH PARTITION

**ATTACH PARTITION 生成逻辑**：
1. 随机选一个分区表
2. 创建一个普通表（结构匹配分区表）
3. `ALTER TABLE parent ATTACH PARTITION child FOR VALUES ...`

**DETACH PARTITION 生成逻辑**：
1. 随机选一个分区表的子分区
2. `ALTER TABLE parent DETACH PARTITION child`

**5.2 CockroachDB HASH 分区**

在现有的 `cockroach_table_option()` 中启用 HASH 分区：

```sql
PARTITION BY HASH (col) PARTITIONS 4;
```

**涉及文件**：

| 文件 | 操作 |
|------|------|
| `src/grammar/grammar.hh` | 修改 — alter_table_stmt 扩展 |
| `src/grammar/grammar.cc` | 修改 — ATTACH/DETACH 实现 + CockroachDB HASH |
| `src/schema/postgres.cc` | 修改 — 启用 has_attach_partition |

**验证**：dry-run 确认 ATTACH/DETACH PARTITION 被生成

---

#### Sprint 6：集成测试 + 版本号 + Release Notes（P2）

**目标**：全量验证 + 发布

- MySQL dry-run 500 queries
- PostgreSQL dry-run 500 queries
- CockroachDB dry-run 100 queries 回归
- 统计分区语句生成频率
- 版本号递增 → v1.0.16
- 更新 `docs/release_notes.md`

---

## 5. 涉及文件总览

### 新增文件：0

### 修改文件（7 个）

| 文件 | Sprint | 变更说明 |
|------|:------:|----------|
| `src/schema/schema.hh` | S1 | 新增 6 个 feature flags |
| `src/grammar/grammar.hh` | S1, S3-S5 | alter_table_stmt 扩展 + create_table_stmt 新增成员 + partition_info 结构 |
| `src/grammar/grammar.cc` | S1-S5 | `mysql_partition_option()` + `pg_partition_option()` + alter_table_stmt 扩展 + 分区选择 |
| `src/schema/mysql.cc` | S1-S3 | 启用分区相关 feature flags |
| `src/schema/postgres.cc` | S4-S5 | 启用分区相关 feature flags |
| `CMakeLists.txt` | S6 | 版本号 |
| `docs/release_notes.md` | S6 | 变更记录 |

---

## 6. 风险评估

| 风险 | 影响 | 缓解措施 |
|------|------|----------|
| MySQL 分区键不在主键中导致建表失败 | 高 | 分区列优先选主键列；非主键列时改复合主键 |
| PG 子分区与父表结构不匹配 | 中 | 子分区由 `PARTITION OF` 创建，结构自动继承 |
| RANGE 分区值不单调递增 | 中 | 使用递增步长 `start + dx(step)` 保证单调 |
| HASH 分区 MODULUS/REMAINDER 计算错误 | 低 | REMAINDER 严格 < MODULUS |
| ALTER TABLE 分区管理操作非分区表 | 高 | 通过 `table_partitions` map 检查目标表是否为分区表 |
| 分区表 + 外键冲突 | 中 | MySQL 分区表不生成外键 |
| 阻抗反馈误杀分区语句 | 低 | 分区语句失败率本身较高，可适当放宽阈值 |

---

## 7. 验收标准

- [ ] MySQL: RANGE / LIST / HASH / KEY / RANGE COLUMNS / LIST COLUMNS 6 种分区类型均可生成
- [ ] MySQL: RANGE+HASH / RANGE+KEY / LIST+HASH / LIST+KEY 4 种子分区组合均可生成
- [ ] MySQL: ALTER TABLE ADD/DROP/TRUNCATE/COALESCE/REORGANIZE PARTITION 可生成
- [ ] MySQL: SELECT/UPDATE/DELETE ... PARTITION(p0) 可生成
- [ ] PostgreSQL: RANGE / LIST / HASH 3 种分区类型均可生成
- [ ] PostgreSQL: CREATE TABLE ... PARTITION OF ... FOR VALUES 子分区语句可生成
- [ ] PostgreSQL: DEFAULT 分区可生成
- [ ] PostgreSQL: 二级分区可生成
- [ ] PostgreSQL: ALTER TABLE ATTACH/DETACH PARTITION 可生成
- [ ] CockroachDB: HASH 分区可生成
- [ ] dry-run MySQL 500 queries 无分区相关语法错误
- [ ] dry-run PostgreSQL 500 queries 无分区相关语法错误
- [ ] PG 回归测试通过（已有功能不受影响）
