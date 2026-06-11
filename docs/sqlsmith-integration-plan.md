# sqlsmith PostgreSQL 功能整合方案

> 版本：dbfuzz v1.0.11 vs sqlsmith (master)
> 日期：2026-06-09

## 目录

- [1. 分析结论](#1-分析结论)
- [2. PostgreSQL 语法覆盖对比（代码级）](#2-postgresql-语法覆盖对比代码级)
- [3. 两者共同缺失的 PostgreSQL 特性](#3-两者共同缺失的-postgresql-特性)
- [4. 整合方案设计](#4-整合方案设计)
- [5. 整合方案 vs 独立工具的优劣势对比](#5-整合方案-vs-独立工具的优劣势对比)
- [6. 实施计划](#6-实施计划)
- [7. 风险与缓解](#7-风险与缓解)

---

## 1. 分析结论

### 核心发现

**dbfuzz 与 sqlsmith 在 PostgreSQL 语法生成上的重叠度远超预期。** 两者均源自 sqlsmith 引擎，经过逐行代码级比对：

| 分类 | 数量 | 说明 |
|------|------|------|
| 两者都已实现的 PG 高级特性 | **15 项** | LATERAL、MERGE、UPSERT、TABLESAMPLE、RETURNING、FOR UPDATE 等 |
| 两者都缺失的 PG 特性 | **10 项** | 数据修改 CTE、量化比较、窗口帧、GROUPING SETS 等 |
| dbfuzz 独有（sqlsmith 无） | **7 项** | TRIGGER、ALTER TABLE、UNION/INTERSECT/EXCEPT 等 |
| sqlsmith 独有（dbfuzz 无） | **0 项** | 无 |

**结论：sqlsmith 没有任何 PostgreSQL 语法生成能力是 dbfuzz 不具备的。** dbfuzz 是 sqlsmith 的严格超集。所谓"整合"的实际含义不是把 sqlsmith 的功能搬过来，而是：

1. **共同补齐** 两者都缺失的 10 项 PostgreSQL 高级特性
2. **引入 sqlsmith 的运行模式**（单进程高频循环 + 错误驱动 oracle）作为 dbfuzz 的第四种测试模式
3. **借鉴 sqlsmith 的设计哲学**（极简执行、高吞吐、快速反馈）

---

## 2. PostgreSQL 语法覆盖对比（代码级）

### 2.1 两者都已实现的特性

| # | PostgreSQL 特性 | sqlsmith 实现 | dbfuzz 实现 | 差异说明 |
|---|----------------|--------------|------------|---------|
| 1 | **LATERAL 子查询** | `table_subquery` + `lateral_subquery` (grammar.cc:81-87) | `table_subquery` + `lateral_subquery` (grammar.cc:159-280) | 相同，dbfuzz 实现更完整 |
| 2 | **TABLESAMPLE** | `table_sample` (grammar.cc:60-79) SYSTEM/BERNOULLI | `table_sample` (grammar.cc:138-157) SYSTEM/BERNOULLI | 完全相同 |
| 3 | **RETURNING 子句** | `delete_returning` (grammar.cc:368-372), `update_returning` (grammar.cc:445-450) | `delete_returning` (grammar.cc:693-697), `update_returning` (grammar.cc:928-933) | 完全相同 |
| 4 | **FOR UPDATE/SHARE** | `select_for_update` (grammar.cc:284-312) 4 种锁模式 | `select_for_update` (grammar.cc:461-489) 4 种锁模式 | 完全相同 |
| 5 | **UPSERT (ON CONFLICT)** | `upsert_stmt` (grammar.cc:453-464) ON CONFLICT ON CONSTRAINT | `upsert_stmt` (grammar.cc:936-947) ON CONFLICT ON CONSTRAINT | 完全相同 |
| 6 | **MERGE 语句** | `merge_stmt` (grammar.cc:541-583) WHEN MATCHED/NOT MATCHED | `merge_stmt` (grammar.cc:996-1038) WHEN MATCHED/NOT MATCHED | 完全相同 |
| 7 | **CTE (WITH 子句)** | `common_table_expression` (grammar.cc:490-539) | `common_table_expression` (grammar.cc:957-994) | 相同，均仅支持只读 SELECT |
| 8 | **窗口函数** | `window_function` (expr.cc:337-383) PARTITION BY + ORDER BY | `window_function` (window_function.cc:4-78) PARTITION BY + ORDER BY | 相同，均无帧子句 |
| 9 | **CASE 表达式** | `case_expr` (expr.cc:42-75) | `case_expr` (case_expr.cc:4-103) | dbfuzz 实现更丰富 |
| 10 | **COALESCE/NULLIF** | `coalesce` (expr.cc:163-193), `nullif` (expr.hh:81-86) | `coalesce` (coalesce.cc:3-81) | 完全相同 |
| 11 | **EXISTS 子查询** | `exists_predicate` (expr.cc:119-135) | `exists_predicate` (exists_predicate.cc:4-262) | dbfuzz 支持 IN 变换 |
| 12 | **IS DISTINCT FROM** | `distinct_pred` (expr.cc:137-141) | `distinct_pred` (distinct_pred.cc:3-13) | 完全相同 |
| 13 | **标量子查询** | `atomic_subselect` (expr.cc:278-335) | `atomic_subselect` (atomic_subselect.cc:3-76) | 完全相同 |
| 14 | **PREPARE 语句** | `prepare_stmt` (grammar.hh:153-167) — 已定义但 statement_factory 未调用 | `prepare_stmt` (grammar.hh:226-240) — 同样未调用 | 两者均未实际生成 |
| 15 | **FULL OUTER JOIN** | `joined_table` (grammar.cc:153-179) | `joined_table` (grammar.cc:236-274) + `supported_join_op` 包含 "full outer" | dbfuzz 支持更多 JOIN 类型 |

### 2.2 dbfuzz 独有（sqlsmith 不具备）

| # | 特性 | dbfuzz 实现位置 | sqlsmith 状态 |
|---|------|---------------|-------------|
| 1 | **CREATE TRIGGER** | `create_trigger_stmt` (grammar.cc:1823-1884) | 未实现 |
| 2 | **UNION/INTERSECT/EXCEPT** | `unioned_query` (grammar.cc:1940-2067) 含 ALL 变体 | 未实现 |
| 3 | **ALTER TABLE** (RENAME/ADD COLUMN) | `alter_table_stmt` (grammar.cc:1622-1696) | 未实现 |
| 4 | **CREATE VIEW** | `create_view_stmt` (grammar.cc:1512-1528) | 未实现 |
| 5 | **ANALYZE 语句** | `analyze_stmt` (grammar.cc:2146-2166) | 未实现 |
| 6 | **SET 会话参数** | `set_stmt` (grammar.cc:2168-2182) + 丰富的 planner 配置 | 未实现 |
| 7 | **部分索引** (WHERE 子句) | `create_index_stmt` (grammar.cc:1726-1822) | 未实现 |
| 8 | **INSERT SELECT** | `insert_select_stmt` (grammar.cc:2069-2144) | 未实现 |
| 9 | **BETWEEN 运算符** | `between_op` (between_op.cc:5-96) | 未实现 |
| 10 | **LIKE 运算符** | `like_op` (like_op.cc:3-66) | 未实现 |
| 11 | **NOT 表达式** | `not_expr` (not_expr.hh:5-17) | 未实现 |
| 12 | **命名窗口** + 引用 | `named_window` (grammar.cc:1886-1938) + `win_func_using_exist_win` | 未实现 |
| 13 | **Planner 优化器配置** | `supported_setting` (postgres.cc:283-322) 含 enable_* 开关 | 未实现 |
| 14 | **复合查询变换** | INTERSECT/EXCEPT 等价变换用于 EET 模式 | 不适用 |
| 15 | **QCN 等价性测试** | 5 种 QCN 测试器 | 无（sqlsmith 仅崩溃检测） |
| 16 | **事务依赖图分析** | TxCheck 模块 | 无 |
| 17 | **两阶段最小化** | `minimize()` | 无 |

### 2.3 Schema 提取对比

| 目录对象 | sqlsmith | dbfuzz | 差异 |
|----------|---------|--------|------|
| `pg_type` (类型系统) | 完整提取（含 pseudo-type, array, range, enum） | 完整提取（同上） | 一致 |
| `pg_operator` (运算符) | 完整提取 | 完整提取 | 一致 |
| `pg_proc` (函数) | 完整提取（过滤 trigger/internal/set-returning） | 完整提取（同上） | 一致 |
| `pg_proc` (聚合) | 完整提取（过滤 percentile_cont/dense_rank 等） | 完整提取（同上） | 一致 |
| `pg_proc` (窗口函数) | 未单独分类 | 独立提取为 `windows` 向量 | dbfuzz 更优 |
| `pg_constraint` (约束) | 提取 (f/u/p) 用于 UPSERT | 提取 (f/u/p) 用于 UPSERT | 一致 |
| `information_schema.tables` | 提取表/视图 | 提取表/视图 | 一致 |
| `pg_attribute` (列) | 提取列名和类型 | 提取列名和类型，排除系统列 | dbfuzz 更精确 |
| Planner 配置 | 未提取 | 提取 `supported_setting` 列表 | dbfuzz 独有 |

---

## 3. 两者共同缺失的 PostgreSQL 特性

以下是 sqlsmith 和 dbfuzz **都没有实现**的 PostgreSQL 高级语法特性，也是"整合"真正需要补齐的内容：

| # | PostgreSQL 特性 | 版本要求 | 语法示例 | 实现复杂度 | Bug 检测价值 |
|---|----------------|---------|---------|-----------|------------|
| 1 | **数据修改 CTE** | 9.1+ | `WITH ins AS (INSERT ... RETURNING *) SELECT * FROM ins` | 中 | **高** — 涉及写操作排序和可见性 |
| 2 | **量化比较** (ALL/ANY/SOME) | 全版本 | `x > ALL (SELECT col FROM t)` | 低 | 中 — 子查询语义边界 |
| 3 | **窗口帧子句** | 全版本 | `SUM(x) OVER (ORDER BY y ROWS BETWEEN 1 PRECEDING AND CURRENT ROW)` | 中 | **高** — 帧边界计算易出错 |
| 4 | **GROUPING SETS/CUBE/ROLLUP** | 全版本 | `GROUP BY GROUPING SETS ((a,b), (a), ())` | 低 | 中 — 分组语义复杂 |
| 5 | **JSON/JSONB 操作** | 9.2+/9.4+ | `jsonb_path_query(col, '$.a')`, `col->>'key'` | **高** | **高** — JSON 路径查询实现复杂 |
| 6 | **数组构造与操作** | 全版本 | `ARRAY[1,2,3]`, `array_agg(x)`, `unnest(arr)` | 中 | 中 — 数组类型边界 |
| 7 | **ROW 构造器** | 全版本 | `ROW(1, 'a') = ROW(col1, col2)` | 低 | 低 — 语法简单 |
| 8 | **范围类型** | 9.2+ | `int4range(1,10)`, `@>`, `&&`, `-|-` | 中 | 中 — 范围运算符丰富 |
| 9 | **分区表 DDL** | 10+ | `CREATE TABLE ... PARTITION BY RANGE (col)` | **高** | 中 — 分区裁剪逻辑 |
| 10 | **GENERATED ALWAYS 列** | 12+ | `col INT GENERATED ALWAYS AS (a+b) STORED` | 低 | 低 — 语法简单 |
| 11 | **EXECUTE 语句** | 全版本 | `PREPARE p AS ...; EXECUTE p;` | 低 | 中 — 计划缓存 |
| 12 | **VALUES 独立语句** | 全版本 | `VALUES (1,'a'), (2,'b')` 作为 FROM 源 | 低 | 低 |

### 优先级评估矩阵

```
Bug 检测价值
  高 │ [P0] 数据修改CTE    [P0] 窗口帧子句    [P1] JSON/JSONB
     │
  中 │ [P1] 量化比较       [P2] 数组操作       [P2] 范围类型
     │ [P2] GROUPING SETS  [P3] EXECUTE
     │
  低 │ [P3] ROW构造器      [P3] VALUES独立     [P3] GENERATED列
     │
     └────────────────────────────────────────────────
       低              中              高
                     实现复杂度
```

---

## 4. 整合方案设计

### 方案 A：渐进式语法补齐（推荐）

在 dbfuzz 现有架构上增量添加缺失的 PostgreSQL 语法特性。

**核心理念：** sqlsmith 没有 dbfuzz 缺少的 PostgreSQL 语法能力。两者的差距不在于"sqlsmith 有而 dbfuzz 没有"，而在于"两者都没有"。因此整合的本质是 **共同演进**。

#### 4.1 P0 级特性（高 Bug 检测价值）

##### 4.1.1 数据修改 CTE (Data-Modifying CTE)

**PostgreSQL 语义：**
```sql
WITH moved_rows AS (
    DELETE FROM source_table WHERE condition RETURNING *
)
INSERT INTO target_table SELECT * FROM moved_rows;
```

**实现方案：**

修改文件：`src/grammar/grammar.hh`, `src/grammar/grammar.cc`

```cpp
// grammar.hh 新增
struct data_modifying_cte : modifying_stmt {
    vector<shared_ptr<prod>> cte_items;  // INSERT/UPDATE/DELETE + RETURNING
    shared_ptr<query_spec> final_query;
    struct scope myscope;
    data_modifying_cte(prod *p, struct scope *s, table *v = 0);
    virtual void out(std::ostream &out);
    virtual void accept(prod_visitor *v);
};

struct cte_modifying_item : prod {
    shared_ptr<prod> dml_stmt;  // insert_stmt | update_stmt | delete_stmt
    shared_ptr<select_list> returning_list;
    cte_modifying_item(prod *p, struct scope *s, table *v);
    virtual void out(std::ostream &out);
};
```

**关键设计点：**
- CTE 内部 DML 必须带 RETURNING 子句（PostgreSQL 要求）
- CTE 结果集在后续 CTE 和最终查询中可见
- 需要扩展 `scope` 类，使 CTE 别名可作为表引用
- 对 EET 模式的影响：数据修改 CTE 改变了表内容，不适合直接用于 QCN 等价变换，但可用于 TxCheck 模式

##### 4.1.2 窗口帧子句 (Window Frame Clause)

**PostgreSQL 语义：**
```sql
SUM(x) OVER (
    PARTITION BY category
    ORDER BY created_at
    ROWS BETWEEN 2 PRECEDING AND CURRENT ROW
)
```

**实现方案：**

修改文件：`src/expr/window_function.hh`, `src/expr/window_function.cc`

```cpp
// window_function.hh 新增
struct window_frame : prod {
    enum frame_mode { ROWS, RANGE, GROUPS };
    enum boundary { UNBOUNDED_PRECEDING, N_PRECEDING, CURRENT_ROW,
                    N_FOLLOWING, UNBOUNDED_FOLLOWING };
    frame_mode mode;
    boundary start;
    int start_offset;
    boundary end;
    int end_offset;
    window_frame(prod *p);
    virtual void out(std::ostream &out);
};
```

**关键设计点：**
- 帧子句仅在 ORDER BY 存在时有意义
- RANGE 模式要求 ORDER BY 仅一列且类型为数值/时间
- ROWS 模式无类型限制
- GROUPS 模式（PostgreSQL 11+）需要版本检测
- 对 EET 模式的价值：窗口帧的边界计算是逻辑 Bug 的高发区

#### 4.2 P1 级特性

##### 4.2.1 量化比较 (ALL/ANY/SOME)

修改文件：`src/expr/bool_expr/` 目录新增

```cpp
// quantified_comparison.hh
struct quantified_comparison : bool_expr {
    enum quantifier { ALL, ANY, SOME };
    shared_ptr<value_expr> lhs;
    string comp_op;
    quantifier quant;
    shared_ptr<query_spec> subquery;
    quantified_comparison(prod *p, struct scope *s);
    virtual void out(std::ostream &out);
};
```

**SQL 输出：** `(expr) > ALL (SELECT col FROM table WHERE ...)`

##### 4.2.2 JSON/JSONB 操作

**实现方案：** 扩展 PostgreSQL schema 提取 + 添加 JSON 表达式

需要修改的文件：
- `src/schema/postgres.cc` — 添加 JSON 函数到 routines 列表
- `src/expr/` — 新增 `json_operator.hh` 处理 `->`, `->>`, `#>`, `#>>` 操作符
- `src/grammar/grammar.cc` — 在 const_expr 中添加 JSON 常量生成

**复杂度高的原因：**
- JSON 类型不是标准 SQL 类型，需要特殊的类型匹配逻辑
- JSON 路径语法 (`jsonb_path_query`) 需要独立的表达式生成器
- 运算符重载多（`->` 返回 JSON，`->>` 返回 text）

##### 4.2.3 GROUPING SETS/CUBE/ROLLUP

修改文件：`src/grammar/grammar.cc` 中的 `group_clause`

```cpp
struct group_clause : prod {
    enum group_type { SIMPLE, GROUPING_SETS, CUBE, ROLLUP };
    group_type type;
    vector<vector<shared_ptr<column_reference>>> group_sets;
    // ...
};
```

**SQL 输出：**
```sql
GROUP BY GROUPING SETS ((a, b), (a), (b), ())
GROUP BY CUBE (a, b, c)
GROUP BY ROLLUP (a, b)
```

#### 4.3 P2-P3 级特性（后续迭代）

| 特性 | 实现文件 | 估计工作量 |
|------|---------|-----------|
| 数组操作 | `expr/array_expr.hh/.cc` | 2 人天 |
| 范围类型 | `schema/postgres.cc` + `expr/range_expr.hh/.cc` | 3 人天 |
| ROW 构造器 | `expr/row_constructor.hh/.cc` | 0.5 人天 |
| EXECUTE | `grammar/grammar.hh` + `grammar.cc` | 0.5 人天 |
| VALUES 独立语句 | `grammar/grammar.cc` | 0.5 人天 |
| GENERATED ALWAYS | `grammar/grammar.cc` DDL 部分 | 1 人天 |
| 分区表 DDL | `grammar/grammar.cc` DDL 部分 | 3 人天 |

### 方案 B：引入 sqlsmith 运行模式（作为第四种测试模式）

**核心理念：** 不是把 sqlsmith 的功能搬过来，而是把 sqlsmith 的**运行策略**（单进程高频循环 + 纯错误驱动 oracle）作为 dbfuzz 的新模式。

#### sqlsmith 运行模式特点

```
┌─────────────────────────────────────────┐
│          sqlsmith 运行模式               │
│                                         │
│  1. 单进程，无 fork                      │
│  2. BEGIN → 随机语句 → ROLLBACK          │
│  3. statement_timeout = 1s              │
│  4. 纯错误检测（crash/timeout/assertion） │
│  5. 吞吐量 200-300 queries/sec          │
│  6. 连接断开 = crash 信号                │
│  7. 持续运行直到手动停止                  │
└─────────────────────────────────────────┘
```

#### 与 dbfuzz 现有模式的对比

| 维度 | EET 模式 | TxCheck 模式 | Cross 模式 | **sqlsmith 模式（新增）** |
|------|---------|-------------|-----------|------------------------|
| 检测目标 | 逻辑 Bug | 事务异常 | 跨库差异 | **Crash/Timeout** |
| 语句包裹 | 无事务 | BEGIN...COMMIT | BEGIN...COMMIT | **BEGIN...ROLLBACK** |
| 执行模型 | fork 子进程 | fork 子进程 | fork 子进程 | **单进程循环** |
| 吞吐量 | 低（~10 轮/分） | 极低（~1 轮/分） | 极低 | **高（200+ queries/sec）** |
| 适用场景 | 深度 Bug 检测 | 事务正确性 | 兼容性测试 | **快速冒烟、压力测试** |
| 假阳性率 | 中（需人工验证） | 低 | 高 | **极低（仅 crash）** |

#### 实现方案

```cpp
// src/smoke/smoke_main.hh — 新增模块
struct smoke_options {
    int timeout_sec = 1;         // statement_timeout
    int total_queries = 0;       // 0 = 无限循环
    bool rollback = true;        // ROLLBACK 模式
    string target_dbms;          // 目标数据库
};

// src/smoke/smoke_run.cc
int smoke_run(dbms_info &d_info, map<string,string> &options) {
    // 单进程循环
    while (!should_stop()) {
        try {
            dut->connection->execute("BEGIN");
            dut->connection->execute("SET statement_timeout = '1s'");
            
            // 生成并执行随机语句
            auto stmt = statement_factory(scope);
            ostringstream os;
            os << *stmt;
            dut->test(os.str());
            
            dut->connection->execute("ROLLBACK");
            impedance_feedback(stmt, true);  // 成功反馈
        } catch (db_connection_error &e) {
            // 连接断开 = 可能的 crash
            save_crash_report(stmt, e);
            // 重连
            dut->reset();
        } catch (query_timeout &e) {
            impedance_feedback(stmt, false);
            dut->connection->execute("ROLLBACK");
        }
    }
}
```

新增文件：
- `src/smoke/smoke_main.hh`
- `src/smoke/smoke_run.cc`
- `src/CMakeLists.txt` 新增 `smoke_objs` 模块

### 方案 C：完整代码合并（不推荐）

将 sqlsmith 源码直接合并到 dbfuzz 代码库。

**不推荐的原因：**
- sqlsmith 没有任何 dbfuzz 缺少的 PostgreSQL 语法能力（见第 2.3 节结论）
- 合并会引入代码冗余和维护负担
- sqlsmith 的单进程模型与 dbfuzz 的 fork 模型架构冲突
- sqlsmith 的阻抗反馈机制已被 dbfuzz 完整继承

---

## 5. 整合方案 vs 独立工具的优劣势对比

### 5.1 方案对比总表

| 维度 | 方案 A：语法补齐 | 方案 B：新增 Smoke 模式 | 独立使用 sqlsmith |
|------|----------------|----------------------|-----------------|
| **PostgreSQL 语法覆盖** | ✅ 最全（补齐后超越 sqlsmith） | ➖ 不增加语法 | ❌ 当前已有（不增长） |
| **代码维护** | ✅ 单一代码库 | ✅ 单一代码库 | ❌ 两套代码库需同步 |
| **Bug 检测深度** | ✅ QCN + 依赖图 + crash | ✅ 高频 crash 扫描 | ⚠️ 仅 crash |
| **吞吐量** | ➖ 中等 | ✅ 200+ qps | ✅ 200-300 qps |
| **DBMS 覆盖** | ✅ 11 个 | ✅ 11 个 | ❌ 仅 3 个 |
| **Bug 复现** | ✅ 完整最小化 | ➖ 仅 SQL 日志 | ⚠️ RNG 重放 |
| **开发工作量** | ⚠️ P0: 2 人周, P1: 1 人周 | ⚠️ 1 人周 | ✅ 零 |
| **学习曲线** | ✅ 统一 CLI | ✅ 统一 CLI | ❌ 两套工具 |
| **社区生态** | ➖ dbfuzz 社区 | ➖ dbfuzz 社区 | ✅ sqlsmith 社区（活跃） |
| **独立部署** | ❌ 需要完整 dbfuzz | ❌ 需要完整 dbfuzz | ✅ 轻量独立工具 |
| **快速冒烟测试** | ⚠️ EET/TxCheck 模式偏重 | ✅ 完美匹配 | ✅ 原生能力 |
| **CI/CD 集成** | ⚠️ 较重 | ✅ 轻量 | ✅ 轻量 |

### 5.2 整合方案（A+B）的优势

1. **统一的 Bug 检测流水线**

```
  ┌──────────────────────────────────────────────────┐
  │            dbfuzz 统一测试流水线                   │
  │                                                   │
  │  Phase 1: Smoke 模式（方案 B）                     │
  │  ├── 高频随机 SQL 轰炸（200+ qps）                 │
  │  ├── 快速发现 crash/timeout/assertion              │
  │  └── 10 分钟冒烟 → 通过后进入深度测试               │
  │                                                   │
  │  Phase 2: EET 模式（已有）                         │
  │  ├── QCN 等价性测试                                │
  │  ├── 5 种变换器（SELECT/UPDATE/DELETE/CTE/INSERT） │
  │  └── 发现逻辑 Bug                                  │
  │                                                   │
  │  Phase 3: TxCheck 模式（已有）                     │
  │  ├── 事务依赖图分析                                │
  │  ├── 6 种异常检测（G1a/G1b/G1c/G2/G-SI）          │
  │  └── 发现事务隔离 Bug                              │
  │                                                   │
  │  Phase 4: Cross 模式（已有）                       │
  │  ├── 跨库结果比较                                  │
  │  └── 发现兼容性差异                                │
  └──────────────────────────────────────────────────┘
```

2. **共享阻抗反馈和 Schema 基础设施**

所有模式共用同一套：
- Schema 提取（pg_type/pg_operator/pg_proc/pg_constraint）
- 阻抗反馈（AST 节点成功率追踪 + 黑名单）
- SQL 生成器（grammar/expr 模块）
- DUT 连接管理（libpqxx/libpq 连接池）

新增的 PostgreSQL 语法特性（数据修改 CTE、窗口帧等）在**所有 4 种模式**中自动可用。

3. **统一的最小化和复现**

Smoke 模式发现的 crash 可以直接使用 dbfuzz 的两阶段最小化器：
```bash
# 先用 smoke 模式发现 crash
./dbfuzz --mode=smoke --postgres-db=testdb --postgres-port=5432 --timeout=10m

# 然后用现有的复现和最小化基础设施
./dbfuzz --mode=txcheck --reproduce-sql=crash_stmts.sql --min
```

4. **渐进式增强**

不需要一次性完成所有语法补齐。每个 P0/P1 特性可以独立开发、测试、合并，不影响其他模式的运行。

### 5.3 整合方案的劣势

| 劣势 | 说明 | 缓解措施 |
|------|------|---------|
| **编译依赖重** | dbfuzz 需要 libpqxx + mysqlclient + sqlite3 等，sqlsmith 仅需 libpqxx | 通过 CMake 条件编译，PostgreSQL-only 构建仅需 libpqxx |
| **启动开销** | dbfuzz fork 模型比 sqlsmith 单进程重 | Smoke 模式采用单进程模型，无 fork |
| **代码库复杂度** | dbfuzz 7 模块 + 新增 smoke 模块 = 8 模块 | 模块边界清晰，smoke 模块最小化 |
| **构建时间** | 完整构建 ~2 分钟（sqlsmith ~30 秒） | CMake 增量编译，修改单文件 <5 秒 |
| **调试复杂度** | 多模式 + 多 DBMS 的交互复杂 | 每种模式独立测试，CI 按模式矩阵运行 |

### 5.4 独立使用 sqlsmith 的唯一合理场景

| 场景 | 理由 | 建议 |
|------|------|------|
| **PostgreSQL 专属快速冒烟** | 只需 30 秒启动，快速验证 PG 是否 crash | 在 dbfuzz 中实现 Smoke 模式后不再需要 |
| **sqlsmith 社区贡献** | 向 sqlsmith 上游提交 PostgreSQL Bug | 发现的 Bug 可以同时报告给 sqlsmith 和 PG 社区 |
| **轻量级 CI 检查** | Docker 镜像小，构建快 | 可构建 dbfuzz-smoke-only 精简镜像 |

---

## 6. 实施计划

### 6.1 总体时间线

```
  Week 1-2  │ P0: 窗口帧子句 + 数据修改 CTE
  Week 3    │ P1: 量化比较 + GROUPING SETS/CUBE/ROLLUP
  Week 4    │ Smoke 模式核心框架
  Week 5    │ P1: JSON/JSONB 基础支持
  Week 6-7  │ P2: 数组操作 + 范围类型 + EXECUTE
  Week 8    │ 集成测试 + 文档更新
```

### 6.2 各特性实施详情

#### 6.2.1 窗口帧子句（P0，预计 3 人天）

**修改文件：**
- `src/expr/window_function.hh` — 新增 `window_frame` 结构体
- `src/expr/window_function.cc` — 在 `out()` 中追加帧子句
- `src/expr/win_funcall.cc` — 同步支持帧子句

**测试策略：**
- EET 模式验证：窗口帧的 SUM/COUNT 在不同帧范围下的结果等价性
- Smoke 模式验证：随机帧边界组合是否触发 crash

**PG 兼容性注意：**
- RANGE 模式仅支持 PostgreSQL
- GROUPS 模式需要 PostgreSQL 11+
- MySQL/SQLite 不支持窗口帧，需要 `#ifdef` 或 DBMS 特性标记

#### 6.2.2 数据修改 CTE（P0，预计 5 人天）

**修改文件：**
- `src/grammar/grammar.hh` — 新增 `data_modifying_cte` 类
- `src/grammar/grammar.cc` — 实现构造函数和 output
- `src/core/relmodel.hh` — scope 扩展支持 CTE 别名引用
- `src/schema/schema.cc` — 生成 CTE 相关索引

**测试策略：**
- TxCheck 模式：验证 CTE 中 DELETE + INSERT 的事务一致性
- Smoke 模式：高频生成数据修改 CTE 压力测试

**PG 兼容性注意：**
- 仅 PostgreSQL 支持数据修改 CTE
- MySQL/SQLite/ClickHouse 不支持，需要通过 `target_dbms` 检查跳过

#### 6.2.3 Smoke 模式框架（预计 5 人天）

**新增文件：**
- `src/smoke/smoke_main.hh` — 模式入口声明
- `src/smoke/smoke_run.cc` — 主循环实现
- `src/CMakeLists.txt` — 新增 `smoke_objs` 目标

**修改文件：**
- `src/main.cc` — 添加 `--mode=smoke` 分发
- `src/core/dbms_info.hh` — 添加 `MODE_SMOKE` 枚举
- `src/core/dbms_info.cc` — 解析 `--mode=smoke`
- `CLAUDE.md` — 更新模块文档

**核心循环：**
```
1. 建立连接
2. SET statement_timeout
3. BEGIN
4. 生成随机语句 → 执行
5. 成功 → impedance 正反馈 → ROLLBACK → 回到 3
6. 错误 → impedance 负反馈 → ROLLBACK → 回到 3
7. 连接断开 → 记录 crash → 重连 → 回到 1
```

#### 6.2.4 量化比较 ALL/ANY/SOME（P1，预计 2 人天）

**新增文件：**
- `src/expr/bool_expr/quantified_comparison.hh/.cc`

**修改文件：**
- `src/expr/bool_expr/bool_expr.hh` — 工厂函数添加量化比较选项
- `src/expr/expr.hh` — 添加 include

**SQL 生成示例：**
```sql
-- ALL
x > ALL (SELECT col FROM t WHERE cond)
-- ANY  
x = ANY (SELECT col FROM t WHERE cond)
-- SOME
x <= SOME (SELECT col FROM t WHERE cond)
```

#### 6.2.5 GROUPING SETS/CUBE/ROLLUP（P1，预计 2 人天）

**修改文件：**
- `src/grammar/grammar.hh` — 扩展 `group_clause` 结构体
- `src/grammar/grammar.cc` — group_clause::out() 添加多集分组

**SQL 生成示例：**
```sql
GROUP BY GROUPING SETS ((a, b), (a), ())
GROUP BY CUBE (a, b, c)  
GROUP BY ROLLUP (a, b)
-- 可与普通 GROUP BY 混合
GROUP BY a, GROUPING SETS ((b, c), (b), ())
```

### 6.3 CMake 变更

```cmake
# src/CMakeLists.txt 新增
add_library(smoke_objs OBJECT
    smoke/smoke_run.cc
)
target_include_directories(smoke_objs PRIVATE ${CMAKE_SOURCE_DIR}/src)

# 添加到 dbfuzz 链接
add_executable(dbfuzz
    $<TARGET_OBJECTS:smoke_objs>
    # ... 其他模块
)
```

---

## 7. 风险与缓解

### 7.1 技术风险

| 风险 | 概率 | 影响 | 缓解 |
|------|------|------|------|
| 数据修改 CTE 与 TxCheck 依赖分析冲突 | 中 | 高 | TxCheck 的 instrumentor 需要识别 CTE 内的 DML 操作，添加特殊处理分支 |
| JSON 类型系统与现有类型一致性机制冲突 | 高 | 中 | 将 JSON/JSONB 作为独立类型分支处理，不参与标准类型匹配 |
| 窗口帧在 MySQL 中不支持导致跨 DBMS 测试失败 | 低 | 低 | 通过 `target_dbms` 特性标记控制，MySQL 模式跳过帧生成 |
| Smoke 模式高频执行导致连接池耗尽 | 低 | 中 | Smoke 模式使用单连接，不走连接池 |
| 新增语法导致阻抗反馈黑名单膨胀 | 中 | 低 | 新语法节点有独立的阻抗计数器，不影响现有节点 |

### 7.2 维护风险

| 风险 | 概率 | 影响 | 缓解 |
|------|------|------|------|
| 代码库膨胀导致新人上手困难 | 中 | 低 | 保持模块边界清晰，smoke 模块自包含 |
| sqlsmith 上游新增特性需同步 | 低 | 低 | sqlsmith 开发节奏慢，且 dbfuzz 已是超集 |
| 多模式间回归测试成本增加 | 中 | 中 | CI 矩阵化：按 (mode × dbms) 独立运行 |

### 7.3 对比 sqlsmith 独立使用时的风险

| 风险 | 概率 | 影响 | 缓解 |
|------|------|------|------|
| 独立 sqlsmith 发现的 Bug 无法用 dbfuzz 复现 | 低 | 中 | 实现 Smoke 模式后消除此需求 |
| sqlsmith 社区的 PG 版本更新适配 | 低 | 低 | dbfuzz 的 PG 驱动同样持续更新 |
| 两套工具的 Bug 报告格式不统一 | 中 | 低 | 统一到 dbfuzz 的 found_bugs/ 目录格式 |

---

## 附录 A：逐函数代码对应表

| 功能 | sqlsmith 文件:行号 | dbfuzz 文件:行号 | 状态 |
|------|-------------------|-----------------|------|
| LATERAL 子查询 | grammar.cc:81-87 | grammar.cc:159-280 | ✅ 两者一致 |
| TABLESAMPLE | grammar.cc:60-79 | grammar.cc:138-157 | ✅ 两者一致 |
| FOR UPDATE/SHARE | grammar.cc:284-312 | grammar.cc:461-489 | ✅ 两者一致 |
| UPSERT | grammar.cc:453-464 | grammar.cc:936-947 | ✅ 两者一致 |
| MERGE | grammar.cc:541-583 | grammar.cc:996-1038 | ✅ 两者一致 |
| RETURNING (DELETE) | grammar.cc:368-372 | grammar.cc:693-697 | ✅ 两者一致 |
| RETURNING (UPDATE) | grammar.cc:445-450 | grammar.cc:928-933 | ✅ 两者一致 |
| CTE (WITH) | grammar.cc:490-539 | grammar.cc:957-994 | ✅ 两者一致 |
| 窗口函数 | expr.cc:337-383 | window_function.cc:4-78 | ✅ 两者一致 |
| CASE 表达式 | expr.cc:42-75 | case_expr.cc:4-103 | ✅ 两者一致 |
| COALESCE/NULLIF | expr.cc:163-193 | coalesce.cc:3-81 | ✅ 两者一致 |
| EXISTS | expr.cc:119-135 | exists_predicate.cc:4-262 | ✅ 两者一致 |
| IS DISTINCT FROM | expr.cc:137-141 | distinct_pred.cc:3-13 | ✅ 两者一致 |
| 标量子查询 | expr.cc:278-335 | atomic_subselect.cc:3-76 | ✅ 两者一致 |
| PREPARE | grammar.hh:153-167 | grammar.hh:226-240 | ⚠️ 两者均未调用 |
| 阻抗反馈 | impedance.cc:1-246 | core/general_process.cc | ✅ dbfuzz 继承并扩展 |
| pg_type 提取 | postgres.cc:117-139 | postgres.cc:220-244 | ✅ 两者一致 |
| pg_operator 提取 | postgres.cc:204-217 | postgres.cc:402-433 | ✅ 两者一致 |
| pg_proc 提取 | postgres.cc:220-252 | postgres.cc:436-510 | ✅ 两者一致 |
| pg_constraint 提取 | postgres.cc:191-199 | postgres.cc:387-398 | ✅ 两者一致 |

## 附录 B：PostgreSQL 版本特性矩阵

| 特性 | PG 9.1 | PG 9.4 | PG 10 | PG 11 | PG 12 | PG 14 | PG 15 |
|------|--------|--------|-------|-------|-------|-------|-------|
| 数据修改 CTE | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| TABLESAMPLE | ❌ | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ |
| UPSERT (ON CONFLICT) | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| MERGE | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ✅ |
| JSON/JSONB | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| JSON Path | ❌ | ❌ | ❌ | ❌ | ✅ | ✅ | ✅ |
| GENERATED ALWAYS | ❌ | ❌ | ❌ | ❌ | ✅ | ✅ | ✅ |
| 分区表 | ❌ | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ |
| MERGE WHEN 扩展 | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ✅ |
| GROUPS 帧模式 | ❌ | ❌ | ❌ | ✅ | ✅ | ✅ | ✅ |

**建议：** dbfuzz 的 PostgreSQL 驱动应检测服务器版本，对不支持的特性进行条件跳过。

## 附录 C：建议的测试覆盖矩阵

整合后的完整测试矩阵（PostgreSQL 为例）：

| 模式 | 语法特性 | 验证方式 | 预期 Bug 类型 |
|------|---------|---------|-------------|
| **Smoke** | 全量 SQL（含新增语法） | crash/timeout/assertion | 服务器崩溃、内存泄漏、死循环 |
| **EET** | SELECT/UPDATE/DELETE/CTE QCN | 等价性比较 | 逻辑 Bug、优化器错误 |
| **TxCheck** | DML + 事务 | 依赖图环检测 | 隔离级别违规（G1/G2/G-SI） |
| **Cross** | 同一 SQL 多 DBMS 执行 | 结果 diff | 跨库语义差异 |

新增语法在各模式中的适用性：

| 新增语法 | Smoke | EET | TxCheck | Cross |
|----------|-------|-----|---------|-------|
| 数据修改 CTE | ✅ | ❌（改变表内容） | ✅ | ✅ |
| 窗口帧 | ✅ | ✅ | ❌（仅 SELECT） | ✅ |
| 量化比较 | ✅ | ✅ | ❌ | ✅ |
| GROUPING SETS | ✅ | ✅ | ❌ | ✅ |
| JSON/JSONB | ✅ | ⚠️（类型复杂） | ❌ | ✅ |
| 数组操作 | ✅ | ⚠️ | ❌ | ✅ |
