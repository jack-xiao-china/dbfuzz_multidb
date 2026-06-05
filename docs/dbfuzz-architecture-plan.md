# DBFuzz 架构设计方案（v3）

> 融合 TxCheck (OSDI'23) 与 EET (OSDI'24)，构建统一的数据库 fuzzing 工具。
> v3 基于源码深度交叉检视补充了 10 项 P0 级实现细节（env_setting_stmts、printed_expr、De Morgan 变换、二次验证、递归重试、插桩推断、多轮状态恢复、DBMS 语法适配、txn_mode 约束、版本计数器管理）。

---

## 1. 项目命名与定位

**工具名称**：`dbfuzz`（Database Fuzzer）— 覆盖事务语义 bug + 逻辑 bug + crash bug

**仓库**：`https://github.com/jack-xiao-china/dbfuzz_multidb.git` → 本地 `D:\Jack.Xiao\dbtools\dbfuzz_multidb`

**二进制**：单一 `dbfuzz` 可执行文件，`--mode` 参数选择模式

---

## 2. 目录结构

```
dbfuzz_multidb/
├── CMakeLists.txt                 # 顶层 CMake
├── cmake/                         # CMake 模块（FindMySQL, FindClickHouse 等）
├── src/
│   ├── CMakeLists.txt             # 汇总所有子目录
│   ├── main.cc                    # 统一入口：--mode 分发
│   │
│   ├── core/                      # SQLsmith 共享核心
│   │   ├── CMakeLists.txt
│   │   ├── relmodel.cc/hh
│   │   ├── prod.cc/hh             # 合合：添加 component_id, row_output typedef
│   │   ├── random.cc/hh           # 合合：添加 random_string()
│   │   ├── impedance.cc/hh        # 完全相同
│   │   ├── dump.cc/hh             # 完全相同
│   │   ├── log.cc/hh              # 统一 pqxx API
│   │   ├── dbms_info.cc/hh        # 统一选项解析 + test_mode
│   │   └── general_process.cc/hh  # 仅共享函数（见修正3）
│   │
│   ├── grammar/                   # 统一 grammar
│   │   ├── CMakeLists.txt
│   │   ├── grammar.cc/hh          # 合合两套语法扩展
│   │
│   ├── expr/                      # 采用 EET 模块化结构
│   │   ├── CMakeLists.txt
│   │   ├── value_expr.hh/cc       # 基类 + 变换框架
│   │   ├── bool_expr/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── bool_expr.hh/cc
│   │   │   ├── bool_binop/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── bool_binop.hh/cc
│   │   │   │   ├── bool_term.hh/cc
│   │   │   │   ├── comparison_op.hh/cc
│   │   │   │   └── distinct_pred.hh/cc
│   │   │   ├── not_expr.hh/cc
│   │   │   ├── null_predicate.hh/cc
│   │   │   ├── const_bool.hh/cc
│   │   │   ├── in_query.hh/cc
│   │   │   ├── like_op.hh/cc
│   │   │   ├── between_op.hh/cc
│   │   │   ├── exists_predicate.hh/cc
│   │   │   ├── comp_subquery.hh/cc
│   │   │   └── printed_expr.hh/cc
│   │   ├── atomic_subselect.cc/hh
│   │   ├── binop_expr.cc/hh
│   │   ├── case_expr.cc/hh
│   │   ├── coalesce.cc/hh
│   │   ├── column_reference.cc/hh
│   │   ├── const_expr.cc/hh
│   │   ├── funcall.cc/hh
│   │   ├── win_funcall.cc/hh
│   │   ├── window_function.cc/hh
│   │   └─── win_func_using_exist_win.cc/hh
│   │
│   ├── schema/                    # 统一 schema + DUT + 9 个 DBMS
│   │   ├── CMakeLists.txt
│   │   ├── schema.cc/hh           # 合合：EET 扩展 + TxCheck 双命名
│   │   ├── dut.cc/hh              # 统一 dut_base
│   │   ├── postgres.cc/hh
│   │   ├── mysql.cc/hh            # 合合：非阻塞 + env_setting_stmts
│   │   ├── mariadb.cc/hh          # TxCheck 独有
│   │   ├── tidb.cc/hh             # 合合两版本
│   │   ├── sqlite.cc/hh           # EET 独有
│   │   ├── clickhouse.cc/hh       # EET 独有
│   │   ├── oceanbase.cc/hh        # EET 独有
│   │   ├── yugabyte.cc/hh         # EET 独有
│   │   ├── cockroach.cc/hh        # EET 独有
│   │
│   ├── txcheck/                   # TxCheck 独有模块
│   │   ├── CMakeLists.txt
│   │   ├── transaction_test.cc/hh
│   │   ├── instrumentor.cc/hh
│   │   ├── dependency_analyzer.cc/hh
│   │   ├── tx_general_process.cc/hh  # TxCheck 专用 general 函数（修正3）
│   │   └── tx_main.cc
│   │
│   ├── eet/                       # EET 独有模块
│   │   ├── CMakeLists.txt
│   │   ├── qcn_tester/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── qcn_tester.hh/cc
│   │   │   ├── qcn_select_tester.hh/cc
│   │   │   ├── qcn_update_tester.hh/cc
│   │   │   ├── qcn_delete_tester.hh/cc
│   │   │   ├── qcn_cte_tester.hh/cc
│   │   │   └── qcn_insert_select_tester.hh/cc
│   │   ├── eet_general_process.cc/hh  # EET 专用 general 函数（修正3）
│   │   └── eet_main.cc
│   │
│   └── cross/                     # 交叉测试模块
│   │   ├── CMakeLists.txt
│   │   ├── cross_tester.hh/cc
│   │   ├── cross_main.cc
│   │
├── docs/
│   ├── architecture.md
│   ├── txcheck-deep-analysis.md
│   ├── txcheck-vs-eet-comparison.md
│   ├── release_notes.md
│   ├── bugs/
│   └── test/
│
├── script/                        # Docker 测试脚本（9个DBMS）
│   ├── mysql/
│   ├── mariadb/
│   ├── postgres/
│   ├── sqlite/
│   ├── clickhouse/
│   ├── tidb/
│   ├── oceanbase/
│   ├── yugabyte/
│   └── cockroach/
│
├── CLAUDE.md
├── README.md
├── .gitignore
└── known.txt / known_re.txt
```

---

## 3. 核心接口设计

### 3.1 dut_base — 统一 DUT 接口

```cpp
// src/schema/dut.hh
#ifndef DUT_HH
#define DUT_HH
#include <stdexcept>
#include <string>
#include <vector>
#include <map>
#include "prod.hh"

using namespace std;

#define DB_RECORD_FILE "db_setup.sql"

namespace dut {
struct failure : public std::exception {
    std::string errstr;
    std::string sqlstate;
    const char* what() const throw() { return errstr.c_str(); }
    failure(const char *s, const char *sqlstate_ = "") throw() : errstr(), sqlstate() {
        errstr = s; sqlstate = sqlstate_;
    }
};
struct broken : failure {
    broken(const char *s, const char *sqlstate_ = "") throw() : failure(s, sqlstate_) {}
};
struct timeout : failure {
    timeout(const char *s, const char *sqlstate_ = "") throw() : failure(s, sqlstate_) {}
};
struct syntax : failure {
    syntax(const char *s, const char *sqlstate_ = "") throw() : failure(s, sqlstate_) {}
};
}

struct dut_base {
    std::string version;

    // === 通用方法 ===
    virtual void test(const string &stmt,
                      vector<vector<string>>* output = NULL,
                      int* affected_row_num = NULL,
                      vector<string>* env_setting_stmts = NULL) = 0;
    virtual void reset(void) = 0;
    virtual void backup(void) = 0;
    virtual void reset_to_backup(void) = 0;
    virtual void get_content(vector<string>& tables_name,
                             map<string, vector<vector<string>>>& content) = 0;

    // === 事务方法（默认实现，EET 模式 DUT 无需重写）===
    virtual string commit_stmt() { return "COMMIT"; }
    virtual string abort_stmt()  { return "ROLLBACK"; }
    virtual string begin_stmt()  { return "START TRANSACTION"; }

    // === 进程管理（默认空实现）===
    virtual string get_process_id() { return ""; }
};

vector<string> process_dbrecord_into_sqls(string db_record_file);
#endif
```

**设计要点**：
- 事务方法用默认实现 → EET 模式 DUT 不需实现
- `env_setting_stmts` 参数 EET 新增 → TxCheck 模式传 NULL
- `get_process_id()` 仅 TxCheck 的 MySQL/MariaDB 重写

### 3.1.1 env_setting_stmts — DBMS 查询优化器配置（P0-1）

**目的**：消除 DBMS 查询优化器的非确定性。PostgreSQL、YugaByte 等 DBMS 的优化器会根据统计信息随机选择执行计划，同一查询两次执行可能走不同路径，导致结果差异（假阳性）。

**机制**：在每次查询执行前，随机选择一组 DBMS 特定的 planner 配置注入到会话中：

```cpp
// dut_base 接口的 env_setting_stmts 参数
virtual void test(const string &stmt,
                  vector<vector<string>>* output = NULL,
                  int* affected_row_num = NULL,
                  vector<string>* env_setting_stmts = NULL) = 0;
```

**各 DBMS 的设置项**（由 schema 层的 `supported_setting` 提供）：

| DBMS | 典型设置 | 数量 |
|------|---------|------|
| PostgreSQL | `SET enable_hashjoin=on/off`, `SET seq_page_cost=...`, `SET geqo=...`, `SET jit=...`, `SET plan_cache_mode=...` | 40+ |
| YugaByte | `SET yb_enable_optimizer_statistics=on`, `SET yb_enable_base_scans_cost_model=on` | 2 |
| MySQL | `SET optimizer_switch='...'` | 5+ |
| 其他 | 按需扩展 | — |

**使用方式**（EET 模式 `qcn_test()` 中）：
```cpp
vector<string> env_setting_stmts;
// 从 schema 的 supported_setting 中随机选择
for (auto& setting : db_schema->supported_setting) {
    if (d6() <= 4)  // 概率选择
        env_setting_stmts.push_back(setting);
}
dut->test(query, &output, NULL, &env_setting_stmts);
```

**TxCheck 模式**：传 NULL（事务测试不需要同一定优化器配置，因为事务执行和非事务执行使用相同的会话状态）。

### 3.1.2 DBMS 特定错误模式目录（补充说明）

每个 DBMS 的 DUT 实现维护 15-25 个正则表达式，用于区分"预期错误"和"非预期错误"（crash）。触发非预期错误时保存语句并 abort 进程。此机制在 EET 和 TxCheck 中均已实现，合并时需统一保留各 DBMS 的完整错误模式列表。

### 3.2 schema — 双命名机制（修正2）

```cpp
// src/schema/schema.hh（关键新增部分）
struct schema {
    // === 基础字段 ===
    vector<sqltype> types;
    vector<table> tables;
    vector<op> operators;
    vector<routine> routines;
    string dbname;
    
    // === EET 扩展 ===
    static string target_dbms;
    bool enable_partial_index = false;
    bool enable_analyze_stmt = false;
    set<string> compound_operators;
    set<string> supported_join_op;
    set<string> supported_table_engine;
    set<string> supported_setting;
    vector<string> supported_collations;
    
    // === TxCheck 扩展 — 双命名机制 ===
    // TxCheck 模式用 "wkey"（写操作标识），EET 模式用 "vkey"（版本标识）
    // 不硬编码为单一命名，由 require_pkey_wkey 动态决定
    bool require_pkey_wkey = false;  // 由 dbms_info.mode 决定
    
    static constexpr const char* PKEY_IDENT = "pkey";  // 两者统一
    static constexpr const char* WKEY_IDENT = "wkey";  // TxCheck 用
    static constexpr const char* VKEY_IDENT = "vkey";  // EET 用
    
    // 返回当前模式应使用的版本键列名
    string get_version_key_name() const {
        return require_pkey_wkey ? WKEY_IDENT : VKEY_IDENT;
    }
    
    // === 核心方法 ===
    virtual void fill_scope(scope &s) = 0;
    virtual string quote_name(const string &id) { return id; }
    bool has_datetype();
};
```

**双命名决策理由**：
1. 保持 EET 复现脚本兼容（已有 bug 用例中表含 `vkey` 列）
2. 保持 TxCheck 论文证明一致性（Lemma 基于 wkey 语义）
3. 两者语义本质相同（标识数据版本），只是命名偏好不同
4. 运行时动态选择，零冲突

### 3.3 scope 统一（修正7）

EET 的 `scope` 有 `stmt_seq`（唯一 ID 生成）和命名管理基础设施，TxCheck 没有。合并后必须添加：

```cpp
// src/core/relmodel.hh（scope 结构体关键新增）
struct scope {
    scope *parent = NULL;
    vector<string> indexes;
    vector<named_relation*> tables;
    vector<named_relation*> refs;    // 可见列
    schema *schema_ = NULL;
    
    // === EET 新增 — 唯一命名管理 ===
    shared_ptr<map<string,unsigned int>> stmt_seq;  // 唯一 ID 生成器
    
    int new_stmt();       // 生成新语句 ID
    void clear_naming_data();  // 清除命名追踪数据
};
```

### 3.3.1 write_op_id / row_id 全局计数器管理（P0-10）

TxCheck 模式依赖两个全局计数器来标识数据版本，合并后需统一管理：

```cpp
// src/grammar/grammar.cc（全局变量）
int write_op_id = 0;       // 写操作版本号，从 0 开始
static int row_id = 10000; // 行主键标识，从 10000 开始

// INSERT 时：
row_id += 1000;            // 每个 INSERT 递增 1000，确保主键唯一
// UPDATE/INSERT 时：
wkey = write_op_id;        // 记录当前写操作版本
write_op_id++;
```

**管理规则**：
- `write_op_id` 在数据库生成后被写入 `wkey.txt`，跨测试轮次保持连续
- `row_id` 每次 INSERT 递增 1000（留出空间避免哈希冲突）
- EET 模式不使用这两个计数器（`require_pkey_wkey=false`），但代码路径中存在，需确保不产生副作用
- Cross 模式使用与 TxCheck 相同的计数器管理（`require_pkey_wkey=true`）

**关键约束**：`write_op_id` 必须在 `generate_database()` 和 `gen_stmts_for_one_txn()` 中共享递增，不能每轮重置，否则依赖分析中不同轮次的版本标识会冲突。

### 3.4 dbms_info — 运行时模式控制

```cpp
// src/core/dbms_info.hh
enum test_mode {
    MODE_TXCHECK,   // 事务语义测试
    MODE_EET,       // 等价表达式变换测试
    MODE_CROSS,     // 交叉测试（事务 + SELECT 类 EET 变换）
};

struct dbms_info {
    string dbms_name;
    string test_db;
    int test_port;
    string host_addr;
    string inst_path;
    
    // TxCheck 参数
    int ouput_or_affect_num = 1;
    bool can_trigger_error_in_txn = false;
    
    // EET 参数
    int db_test_num = 50;
    int db_table_num = 0;
    
    // 运行模式
    test_mode mode = MODE_EET;
    bool require_pkey_wkey;    // 根据 mode 自动设置
    
    dbms_info(map<string,string>& options);
};
```

| 配置项 | MODE_TXCHECK | MODE_EET | MODE_CROSS |
|--------|-------------|----------|------------|
| `require_pkey_wkey` | `true` → wkey | `false` → vkey | `true` → wkey |
| 版本键列名 | `wkey` | `vkey` | `wkey` |
| DUT 接口 | 含事务方法+非阻塞 | 仅基础 test() | 含事务方法 |
| 语句生成 | 含 BEGIN/COMMIT/ABORT | SELECT/UPDATE/DELETE/CTE | 含事务+含变换(仅SELECT) |
| 测试循环 | multi_stmt_round_test | qcn_test | 事务流程+SELECT变换验证 |

---

## 4. grammar 合合策略

以 EET 的 grammar.cc 为基础（命名管理基础设施 + 更多语法特性），叠加 TxCheck 的事务语句和 wkey 约束。

### 关键合并点

| 语法特性 | 来源 | 合合方式 |
|----------|------|----------|
| `clear_naming_data()` / `unique_column_name()` | EET | 必须采纳 |
| `common_table_expression` (CTE) | EET | 直接采纳 |
| `unioned_query` + intersect/except | EET | 直接采纳 |
| `analyze_stmt` / `set_stmt` | EET | 直接采纳 |
| `create_table_stmt` DBMS 特定选项 | EET | 直接采纳 |
| `create_index_stmt` partial index | EET | 直接采纳 |
| `insert_select_stmt` | EET | 直接采纳 |
| `txn_string_stmt` (BEGIN/COMMIT/ABORT) | TxCheck | 新增到 EET 版本 |
| `update_stmt` 的版本键赋值 | TxCheck | **条件化**：`if (schema::require_pkey_wkey)` 时用 `get_version_key_name()` |
| `insert_stmt` 的版本键赋值 | TxCheck | **条件化**：同上 |
| `txn_statement_factory()` | TxCheck | 新增 |

### txn_mode 参数约束（P0-9）

TxCheck 的语句生成必须启用 `txn_mode=true`，以约束 SQL 生成范围：

```cpp
// grammar.cc — txn_statement_factory() 内部
if (txn_mode == true) {
    use_group = 0;           // 禁止 GROUP BY
    from_clause(p, true);    // 单表 FROM（无 JOIN）
    // SELECT * 全列输出（便于追踪行级读写集）
}
```

**设计原因**：事务测试需要通过插桩追踪行级读写集。多表 JOIN 和 GROUP BY 会使插桩语义变得极其复杂（JOIN 后行标识不明确，GROUP BY 后单行对应多行原始数据），导致依赖分析失效。

**各模式的 txn_mode 设置**：

| 模式 | txn_mode | 原因 |
|------|----------|------|
| MODE_TXCHECK | `true` | 需要行级插桩追踪 |
| MODE_EET | `false` | 单语句测试，无插桩需求 |
| MODE_CROSS | `true`（事务语句）/ `false`（EET 变换的 SELECT） | 事务部分需要插桩，变换部分独立生成 |

### DBMS 特定语法适配清单（P0-8）

grammar.cc 合并时必须保留以下 DBMS 特定的条件分支（`if (schema::target_dbms == "xxx")`）：

| DBMS | 语法特殊性 | 代码位置 |
|------|-----------|----------|
| **ClickHouse** | `ALTER TABLE ... DELETE` 替代 `DELETE FROM`；`ALTER TABLE ... UPDATE` 替代 `UPDATE`；RHS JOIN 不能嵌套 | grammar.cc:686, 918, 238 |
| **SQLite** | 隐藏 `rowid` 列，INSERT/INDEX 需排除；不支持 `RIGHT OUTER JOIN`、`FULL OUTER JOIN` | sqlite.cc:159; grammar.cc:729 |
| **CockroachDB** | 表分区（LIST/RANGE）；`PARTITION BY NOTHING` | grammar.cc:1228-1311 |
| **YugaByte** | `SPLIT INTO N TABLETS` 表选项 | grammar.cc:1313-1349 |
| **TiDB** | `PRE_SPLIT_REGIONS`、`AUTO_ID_CACHE` 表选项 | tidb.cc:153-156 |
| **MySQL** | 仅支持 `UNION`（无 INTERSECT/EXCEPT） | mysql.cc compound_operators |
| **PostgreSQL** | 全量复合运算符（UNION/INTERSECT/EXCEPT + ALL 变体）；40+ planner SET 选项 | postgres.cc compound_operators / supported_setting |
| **ClickHouse** | `supported_table_engine`: MergeTree/ReplacingMergeTree/SummingMergeTree/AggregatingMergeTree/Log/TinyLog；Log 引擎不支持 PRIMARY KEY | clickhouse.cc:238-249 |

**合并原则**：以 EET 的 grammar.cc 为基础（已包含大部分 DBMS 分支），叠加 TxCheck 的事务语句和 wkey 约束。合并后逐一检查上述条件分支是否完整保留。

### DBMS 检测统一

统一为 EET 的运行时 `schema::target_dbms` 方式，但保留 CMake 条件编译用于 DUT 源文件选择。

### include 路径统一（修正4）

所有 `.cc/.hh` 统一使用相对于 `src/` 的路径：

```cpp
// 所有文件统一 include 风格：
#include "core/relmodel.hh"
#include "expr/value_expr.hh"
#include "expr/bool_expr/bool_expr.hh"
#include "grammar/grammar.hh"
#include "schema/dut.hh"
#include "schema/schema.hh"
#include "txcheck/transaction_test.hh"
#include "eet/qcn_tester/qcn_tester.hh"
```

配合 CMake 的 `target_include_directories(dbfuzz PRIVATE ${CMAKE_SOURCE_DIR}/src)` 即可。

---

## 5. expr/value_expr 合合策略

采用 EET 的模块化结构。TxCheck 适配点：

1. `instrumentor.cc` 使用 `update_stmt->search`、`update_stmt->set_list` 等——EET 版本也有这些字段，无需适配
2. `instrumentor.cc` 中查找 `wkey` 列索引 → 改为查找 `get_version_key_name()` 返回的列名
3. `equivalent_transform()` / `back_transform()` 保留接口，TxCheck 模式不调用

### 5.1 printed_expr — 变换循环引用规避（P0-2）

**问题**：EET 的等价变换在某些情况下会创建 AST 循环引用。例如 `bool_expr::equivalent_transform()` 的第三种变换选择：

```
expr → CASE WHEN rand_bool THEN non_eq_trans_expr ELSE expr END
                                              ↑ 指向同一个 expr 对象
```

如果 `expr` 在变换后又引用了自身的 `eq_value_expr`，形成循环 → 打印时无限递归 → 栈溢出。

**解决方案**：`printed_expr` 在变换前捕获表达式的字符串表示，切断循环引用：

```cpp
// src/expr/printed_expr.hh
struct printed_expr : value_expr {
    string printed_str;  // 变换前的字符串快照
    
    printed_expr(prod* p, shared_ptr<value_expr> expr) : value_expr(p) {
        ostringstream sstr;
        sstr << *expr;       // 在变换前序列化
        printed_str = sstr.str();
    }
    
    void out(ostream &out) { out << printed_str; }  // 输出快照，不递归
};
```

**使用时机**：在 `bool_expr::equivalent_transform()` 中，当第三种变换选择被触发时：
```cpp
// d9() > 6 时触发
auto non_eq_trans_expr = make_shared<printed_expr>(this, share_this);
eq_value_expr = make_shared<case_expr>(this, random_bool, non_eq_trans_expr, share_this);
```

### 5.2 De Morgan 定律变换 — bool_term（P0-3）

`bool_term::equivalent_transform()` 是 EET 的核心布尔变换之一，应用 De Morgan 定律：

```
(a OR b)  → NOT (NOT a AND NOT b)
(a AND b) → NOT (NOT a OR NOT b)
```

**变换实现**：
```cpp
void bool_term::equivalent_transform() {
    bool_binop::equivalent_transform();
    
    // 1. 交换操作符：OR → AND，AND → OR
    op = (op == "or") ? "and" : "or";
    
    // 2. 对两侧取 NOT
    auto not_lhs = make_shared<not_expr>(this, dynamic_pointer_cast<bool_expr>(lhs));
    auto not_rhs = make_shared<not_expr>(this, dynamic_pointer_cast<bool_expr>(rhs));
    
    // 3. 构造 NOT(NOT a OP' NOT b)
    auto inner = make_shared<bool_term>(this, op == "or", not_rhs, not_lhs);
    equal_expr = make_shared<not_expr>(this, inner);
    has_equal_expr = true;
}
```

**`back_transform()` 精确逆向**：
```cpp
void bool_term::back_transform() {
    has_equal_expr = false;
    // 从 NOT(bool_term) 中提取 not_expr 子节点
    auto not_outer = dynamic_pointer_cast<not_expr>(equal_expr);
    auto inner_term = dynamic_pointer_cast<bool_term>(not_outer->inner_expr);
    // 恢复原始操作数和操作符
    lhs = dynamic_pointer_cast<not_expr>(inner_term->rhs)->inner_expr;
    rhs = dynamic_pointer_cast<not_expr>(inner_term->lhs)->inner_expr;
    op = (op == "or") ? "and" : "or";  // 交换回来
}
```

**移植注意**：`bool_term` 的变换依赖 `not_expr`，合并时需确保两者接口兼容。`back_transform()` 中的 `dynamic_pointer_cast` 链如果任一环节类型不匹配会返回 NULL，需确保变换和还原使用完全对称的 AST 构造。

---

## 6. general_process 拆分（修正3）

不合并为一个文件，拆分为共享 + 模式专用：

```
src/core/general_process.cc/hh     — 共享函数：
    dut_setup(), get_schema(), save_backup_file(), use_backup_file(),
    make_dir_error_exit(), print_stmt_to_string(), fork_db_server(),
    user_signal(), dut_reset(), dut_backup(), dut_reset_to_backup(),
    dut_get_content(), generate_database()（根据 mode 调用不同子函数）

src/txcheck/tx_general_process.cc/hh — TxCheck 专用：
    BKDRHash(), hash_output_to_set(), nomoalize_content(),
    compare_output(), compare_content()（修正 bug: begin→end）,
    gen_stmts_for_one_txn(), save_current_testcase(),
    minimize_testcase(), reproduce_routine(), check_txn_cycle(),
    txn_decycle_test(), check_topo_sort(), normal_test(), interect_test()

src/eet/eet_general_process.cc/hh   — EET 专用：
    normal_test()（EET 版本）, interect_test()（EET 版本）,
    minimize_qcn_database()
```

**注意**：`generate_database()` 是共享函数，但内部逻辑根据 mode 不同：
- TxCheck/Cross：DDL + DDL 含 pkey/wkey 列 → basic_dml → backup
- EET：DDL（不含 wkey）→ basic_dml → backup + save_dbrecord

实现方式：`generate_database()` 根据 `d_info.require_pkey_wkey` 决定 DDL 生成是否包含版本键列。

### 已知 bug 同步修复（修正8）

在移植 `tx_general_process.cc` 时同步修复：

1. **`compare_content()` 循环条件**：
   - 原 bug：`iter != a_content.begin()` → 永不执行
   - 修正为：`iter != a_content.end()`

2. **`set_intersection` 空范围**（`dependency_analyzer.cc`）：
   - 原 bug：`set_intersection(i_primary_set.begin(), i_primary_set.begin(), ...)` → 空范围
   - 修正为：`set_intersection(i_primary_set.begin(), i_primary_set.end(), before_write_primary_set.begin(), before_write_primary_set.end(), ...)`

### 6.1 multi_stmt_round_test 多轮状态恢复机制（P0-7）

**核心设计**：`multi_stmt_round_test()` 每轮测试一个**不同的**拓扑排序路径。每轮结束后必须恢复原始状态，否则第二轮及后续测试基于被修改的队列执行，结果不可靠。

**状态快照与恢复**：

```cpp
// 测试开始前保存快照
auto init_stmt_queue = stmt_queue;
auto init_stmt_usage = stmt_use;
auto init_tid_queue = tid_queue;
map<int, txn_status> init_txn_status;
map<int, vector<shared_ptr<prod>>> init_trans_arr;
for (int tid = 0; tid < trans_num; tid++) {
    init_txn_status[tid] = trans_arr[tid].status;
    init_trans_arr[tid] = trans_arr[tid].stmts;
}

// 每轮结束后恢复
stmt_queue = init_stmt_queue;
stmt_use = init_stmt_usage;
tid_queue = init_tid_queue;
for (int tid = 0; tid < trans_num; tid++) {
    trans_arr[tid].stmts = init_trans_arr[tid].stmts;
    change_txn_status(tid, init_txn_status[tid]);
}
```

**多轮迭代流程**：
```
Round 1: 拓扑排序 → 取最长路径 → 非事务执行 → 比较 → 从图中删除已测路径节点 → 恢复状态
Round 2: 拓扑排序（图已缩小）→ 取新路径 → 非事务执行 → 比较 → 删除 → 恢复
Round N: 路径为空或连续 3 次空路径 → 结束
```

### 6.2 TxCheck 关键函数补充说明

以下函数在 `transaction_test.cc` 中实现，是 TxCheck 正确性的关键，合并时必须完整移植：

#### retry_block_stmt — 递归重试（P0-5）

**递归语义**：当某事务 commit/abort 释放锁后，其他被阻塞事务的语句可以立即重试：

```cpp
void trans_test_unit(int i, ...) {
    // 执行语句...
    // 返回值: 2=错误/跳过(→SPACE_HOLDER), 1=成功, 0=仍阻塞
    if (is_commit || is_abort) {
        retry_block_stmt(stmt_pos, status_queue, debug_mode);  // 递归！
    }
}
```

递归确保尽可能多的语句被执行，减少被 SPACE_HOLDER 替换的语句数量。不实现递归重试，阻塞调度的语句恢复率会大幅降低。

#### infer_instrument_after_blocking — 插桩数量一致性（P0-6）

阻塞调度会将部分语句替换为 SPACE_HOLDER，导致插桩语句数量与实际语句数量不匹配。此函数通过 `INSTRUMENT_DEPEND` 边查找每个被替换语句的插桩组大小，并插入对应数量的占位符来保持队列一致性：

```cpp
void infer_instrument_after_blocking(...) {
    // 对每个被 SPACE_HOLDER 替换的语句，查找其原始插桩组大小
    auto set_size = instrumented_set.size();  // 原语句有 N 个插桩语句
    for (int k = 0; k < set_size; k++) {
        // 填充 N 个占位符保持队列长度一致
        final_after_stmt_queue.push_back(after_stmt_queue[i]);
    }
}
```

不实现此函数，`multi_stmt_round_test()` 的迭代逻辑会因为队列长度不匹配而崩溃。

---

## 7. 交叉模式修正（修正1 — 最关键）

### 原方案的问题

原设计对 stmt_path 中每条语句独立做 EET 变换，但 stmt_path 中的语句有依赖关系。对写入类语句做变换会导致数据流依赖链断裂。

### 修正后的交叉模式逻辑

**核心约束**：EET 变换**只应用于 SELECT 类语句**（query_spec、CTE），**不应用于写入类语句**（UPDATE/INSERT/DELETE）。

理由：
- SELECT 不改变数据库状态，变换只影响其输出 → 不影响后续语句的依赖前提
- UPDATE/DELETE/INSERT 改变数据库状态，变换后的写入导致不同的数据库内容 → 破坏依赖链

```cpp
// src/cross/cross_main.cc
void cross_run(dbms_info& d_info, shared_ptr<schema> db_schema) {
    // 1. TxCheck 流程：生成事务 → 插桩 → 去环 → 执行 → 依赖分析
    transaction_test t(d_info);
    t.test();  // 包含 assign_txn_id → gen → block_scheduling → 
               // instrument → trans_test → analyze → multi_stmt_round_test
    
    // 2. 获取最终精化后的 stmt_path 和 init_da
    auto stmt_path = init_da->topological_sort_path(deleted_nodes);
    
    // 3. 非事务执行 stmt_path（TxCheck oracle）
    t.normal_stmt_test(stmt_path);
    bool txcheck_bug = !t.check_normal_stmt_result(stmt_path);
    
    // 4. 交叉验证：对 stmt_path 中的 SELECT 类语句做 EET 变换
    //    构建变换后的 stmt_path：
    //    - SELECT/CTE → 做 equivalent_transform
    //    - UPDATE/INSERT/DELETE → 保持原样（不做变换）
    vector<shared_ptr<prod>> transformed_stmts;
    for (auto& sid : stmt_path) {
        auto stmt = get_stmt_from_path(sid, t);
        auto usage = get_stmt_usage_from_path(sid, t);
        
        if (is_select_like_stmt(usage)) {  // SELECT_READ, VERSION_SET_READ
            // SELECT 类语句：做 EET 变换
            auto transformed = equivalent_transform_select(stmt);
            transformed_stmts.push_back(transformed);
        } else {
            // 写入类语句：保持原样
            transformed_stmts.push_back(stmt);
        }
    }
    
    // 5. 非事务执行变换后的 stmt_path
    multiset<row_output> transformed_results;
    for (auto& stmt : transformed_stmts) {
        // 重置到 backup，逐条执行
        dut_reset_to_backup(d_info);
        execute_stmt_without_txn(stmt, transformed_results);
    }
    
    // 6. 三重比较
    //    a. 事务结果 vs 非事务原始结果 → TxCheck oracle
    //    b. 非事务原始结果 vs 非事务变换结果 → EET oracle (仅 SELECT 部分)
    //    c. 事务结果 vs 非事务变换结果 → 交叉 oracle
    
    if (txcheck_bug) {
        // 事务语义 bug
        report_bug("txcheck", ...);
    }
    
    // 比较变换 SELECT 的结果与原始 SELECT 的结果
    if (eet_select_results_differ) {
        // 逻辑 bug（在事务相关 SELECT 中触发）
        report_bug("eet", ...);
    }
    
    // 数据库最终状态比较
    compare_db_content(trans_db_content, normal_db_content);
    compare_db_content(trans_db_content, transformed_db_content);
}
```

**交叉模式的独特价值**：
- 发现"只在事务上下文中的 SELECT 才触发的逻辑 bug"
- 例如：某 SELECT 在事务外返回正确结果，但在事务内因 MVCC 可见性导致子查询结果不同 → 变换后 SELECT 在事务外结果与原始 SELECT 在事务外结果不一致 → 逻辑 bug

### 7.1 假阳性二次验证（P0-4）

DBMS 的非确定性（优化器随机性、并发竞争、浮点精度）可能导致单次比较产生假阳性。所有三种模式（txcheck/eet/cross）在报告 bug 前必须执行二次验证：

**EET 模式的验证**（qcn_select_tester.cc）：
```cpp
if (qit_query_result != original_query_result) {
    // 第一次检测到差异 → 重新执行验证
    execute_query(original_query, original_query_result);  // 重新执行原始查询
    execute_query(qit_query, qit_query_result);            // 重新执行变换查询
    
    if (qit_query_result != original_query_result) {
        // 二次确认仍不一致 → 真 bug
        report_bug("eet", ...);
    } else {
        // 二次执行结果一致 → 假阳性，跳过
    }
}
```

**TxCheck 模式的验证**：
```cpp
if (!check_normal_stmt_result(stmt_path)) {
    // 重新执行事务和非事务序列
    dut_reset_to_backup(d_info);
    trans_test(stmt_path);        // 重新执行事务
    dut_reset_to_backup(d_info);
    normal_stmt_test(stmt_path);  // 重新执行非事务
    
    if (!check_normal_stmt_result(stmt_path)) {
        // 二次确认 → 真 bug
        report_bug("txcheck", ...);
    }
}
```

**Cross 模式的验证**：对三路比较中每一路的不一致结果都执行二次验证。

### 7.2 交叉模式状态管理流程（补充）

交叉模式需要在**同一数据库状态**上执行三次，三次执行之间的状态管理流程：

```
① backup（dut_backup）
     ↓
② 事务执行 → 收集事务执行结果 tx_results
     ↓
③ restore（dut_reset_to_backup）
     ↓
④ 非事务执行原始 stmt_path → 收集 normal_results
     ↓
⑤ restore（dut_reset_to_backup）
     ↓
⑥ 非事务执行变换 stmt_path → 收集 transformed_results
     ↓
⑦ 三路比较（tx vs normal / normal vs transformed / tx vs transformed）
```

**写入语句的 wkey/pkey 处理**：非事务执行时，写入语句（UPDATE/INSERT/DELETE）保持原样不做变换，仅 SELECT 类语句做 EET 变换。这确保了三次执行中数据库状态变更路径一致。

---

## 8. CMake 构建系统

### 顶层 CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.14)
project(dbfuzz VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# === 查找依赖 ===
find_package(Boost REQUIRED COMPONENTS regex)
find_package(libpqxx REQUIRED)

# 可选依赖
find_package(SQLite3 QUIET)
find_package(mysqlclient QUIET)

# PostgreSQL 库（EET 需要）
find_package(PostgreSQL REQUIRED)

# === 条件编译选项 ===
if(SQLite3_FOUND)
    message(STATUS "SQLite3 found: ${SQLite3_VERSION}")
    add_definitions(-DHAVE_LIBSQLITE3)
else()
    message(STATUS "SQLite3 not found, SQLite support disabled")
endif()

if(mysqlclient_FOUND)
    message(STATUS "mysqlclient found")
    add_definitions(-DHAVE_MYSQL -DHAVE_TIDB)
    # 检查非阻塞 API
    include(CheckCXXSymbolExists)
    set(CMAKE_REQUIRED_LIBRARIES mysqlclient)
    check_cxx_symbol_exists(mysql_real_query_nonblocking mysql/mysql.h HAVE_MYSQL_NONBLOCK)
    check_cxx_symbol_exists(mysql_real_query_start mysql/mysql.h HAVE_MARIADB)
    if(HAVE_MYSQL_NONBLOCK)
        add_definitions(-DHAVE_MYSQL_NONBLOCK)
    endif()
    if(HAVE_MARIADB)
        add_definitions(-DHAVE_MARIADB)
    endif()
else()
    message(STATUS "mysqlclient not found, MySQL/TiDB/MariaDB support disabled")
endif()

# === 子目录 ===
add_subdirectory(src)

# === 安装 ===
install(TARGETS dbfuzz RUNTIME DESTINATION bin)
```

### src/CMakeLists.txt

```cmake
# 定义各模块的对象库

# core 模块
add_library(core_objs OBJECT
    core/relmodel.cc
    core/random.cc
    core/prod.cc
    core/impedance.cc
    core/dump.cc
    core/log.cc
    core/dbms_info.cc
    core/general_process.cc
)
target_include_directories(core_objs PRIVATE ${CMAKE_SOURCE_DIR}/src)

# grammar 模块
add_library(grammar_objs OBJECT
    grammar/grammar.cc
)
target_include_directories(grammar_objs PRIVATE ${CMAKE_SOURCE_DIR}/src)

# expr 模块
add_library(expr_objs OBJECT
    expr/value_expr.cc
    expr/atomic_subselect.cc
    expr/window_function.cc
    expr/coalesce.cc
    expr/funcall.cc
    expr/win_func_using_exist_win.cc
    expr/binop_expr.cc
    expr/const_expr.cc
    expr/case_expr.cc
    expr/win_funcall.cc
    expr/printed_expr.cc
    expr/column_reference.cc
    expr/bool_expr/bool_expr.cc
    expr/bool_expr/comp_subquery.cc
    expr/bool_expr/not_expr.cc
    expr/bool_expr/like_op.cc
    expr/bool_expr/between_op.cc
    expr/bool_expr/null_predicate.cc
    expr/bool_expr/exists_predicate.cc
    expr/bool_expr/const_bool.cc
    expr/bool_expr/in_query.cc
    expr/bool_expr/bool_binop/distinct_pred.cc
    expr/bool_expr/bool_binop/bool_term.cc
    expr/bool_expr/bool_binop/bool_binop.cc
    expr/bool_expr/bool_binop/comparison_op.cc
)
target_include_directories(expr_objs PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/expr
    ${CMAKE_SOURCE_DIR}/src/expr/bool_expr
    ${CMAKE_SOURCE_DIR}/src/expr/bool_expr/bool_binop
)

# schema + DUT 模块
set(SCHEMA_DUT_SOURCES
    schema/schema.cc
    schema/dut.cc
    schema/postgres.cc
    schema/clickhouse.cc
    schema/yugabyte.cc
    schema/cockroach.cc
)
if(mysqlclient_FOUND)
    list(APPEND SCHEMA_DUT_SOURCES
        schema/mysql.cc
        schema/tidb.cc
        schema/oceanbase.cc
    )
    if(HAVE_MARIADB)
        list(APPEND SCHEMA_DUT_SOURCES schema/mariadb.cc)
    endif()
endif()
if(SQLite3_FOUND)
    list(APPEND SCHEMA_DUT_SOURCES schema/sqlite.cc)
endif()

add_library(schema_objs OBJECT ${SCHEMA_DUT_SOURCES})
target_include_directories(schema_objs PRIVATE ${CMAKE_SOURCE_DIR}/src)

# txcheck 模块
add_library(txcheck_objs OBJECT
    txcheck/transaction_test.cc
    txcheck/instrumentor.cc
    txcheck/dependency_analyzer.cc
    txcheck/tx_general_process.cc
    txcheck/tx_main.cc
)
target_include_directories(txcheck_objs PRIVATE ${CMAKE_SOURCE_DIR}/src)

# eet 模块
add_library(eet_objs OBJECT
    eet/qcn_tester/qcn_tester.cc
    eet/qcn_tester/qcn_select_tester.cc
    eet/qcn_tester/qcn_update_tester.cc
    eet/qcn_tester/qcn_delete_tester.cc
    eet/qcn_tester/qcn_cte_tester.cc
    eet/qcn_tester/qcn_insert_select_tester.cc
    eet/eet_general_process.cc
    eet/eet_main.cc
)
target_include_directories(eet_objs PRIVATE ${CMAKE_SOURCE_DIR}/src)

# cross 模块
add_library(cross_objs OBJECT
    cross/cross_tester.cc
    cross/cross_main.cc
)
target_include_directories(cross_objs PRIVATE ${CMAKE_SOURCE_DIR}/src)

# 链接为单一可执行文件
add_executable(dbfuzz
    main.cc
    $<TARGET_OBJECTS:core_objs>
    $<TARGET_OBJECTS:grammar_objs>
    $<TARGET_OBJECTS:expr_objs>
    $<TARGET_OBJECTS:schema_objs>
    $<TARGET_OBJECTS:txcheck_objs>
    $<TARGET_OBJECTS:eet_objs>
    $<TARGET_OBJECTS:cross_objs>
)

target_link_libraries(dbfuzz
    Boost::regex
    libpqxx::pqxx
    PostgreSQL::PostgreSQL
)
if(mysqlclient_FOUND)
    target_link_libraries(dbfuzz mysqlclient)
endif()
if(SQLite3_FOUND)
    target_link_libraries(dbfuzz SQLite::SQLite3)
endif()
```

---

## 9. 命令行接口

```
./dbfuzz --mode=<txcheck|eet|cross> --<dbms>-db=<dbname> [其他选项]

# 通用选项
--mode=txcheck|eet|cross     测试模式（必选）
--seed=int                   随机种子
--cpu-affinity=int           CPU 核绑定
--ignore-crash               忽略 crash 继续测试
--help                       帮助

# DBMS 连接（至少选一个）
--mysql-db/port              MySQL
--mariadb-db/port            MariaDB
--postgres-db/port/path      PostgreSQL
--sqlite=<file>              SQLite
--clickhouse-db/port         ClickHouse
--tidb-db/port               TiDB
--oceanbase-db/port/host     OceanBase
--yugabyte-db/port/host      Yugabyte
--cockroach-db/port/host     CockroachDB

# TxCheck 专用（mode=txcheck/cross）
--output-or-affect-num=int   语句输出/影响行数下限（默认1）
--reproduce-sql/tid/usage/backup  复现参数
--min                        最小化测试用例

# EET 专用（mode=eet/cross）
--db-test-num=int            每个数据库的测试轮数（默认50）
--db-table-num=int           每个数据库的表数量
```

---

## 10. Bug 报告格式

```
found_bugs/
├── bug_0_txcheck/    # 事务 bug（与原 TxCheck 格式一致）
│   ├── final_stmts.sql, final_tid.txt, final_stmt_use.txt, db_backup.sql
│
├── bug_1_eet/        # 逻辑 bug（与原 EET 格式一致）
│   ├── origin.sql, eet.sql, origin.out, eet.out, db_setup.sql, env_stmts.sql
│
├── bug_2_cross/      # 交叉 bug（新格式）
│   ├── tx_stmts.sql, normal_stmts.sql, eet_select_stmts.sql
│   ├── tx_results.out, normal_results.out, eet_select_results.out
│   ├── db_backup.sql
│
└── bug_3_crash/      # crash bug
    ├── unexpected.sql, unexpected.err, db_setup.sql
```

---

## 11. 实施路线（v3）

### Phase 0：基础搭建 + CI（~1周）

| 步骤 | 工作内容 | 验证标准 |
|------|----------|----------|
| 0.1 | Clone dbfuzz_multidb 仓库 | `git clone` 成功 |
| 0.2 | 创建完整目录结构 | 目录树与 Section 2 一致 |
| 0.3 | 复制 EET 的 core 文件，修改 include 路径为 `src/` 相对 | 编译通过 |
| 0.4 | 复制 EET 的 expr 模块化结构（含 `printed_expr.cc/hh`），修改 include 路径 | 编译通过 |
| 0.5 | 复制 EET 的 schema + dut + DBMS 实现（含各 DBMS 的 `supported_setting` 列表） | 编译通过 |
| 0.6 | 编写 CMakeLists.txt（顶层 + src/ + 各子目录），确保 `printed_expr.cc` 被编译 | `cmake -B build && cmake --build build` 成功 |
| 0.7 | 编写 main.cc 骨架（先 EET 模式） | `dbfuzz --mode=eet` 可连接 PG |
| 0.8 | 建立 GitHub Actions CI：每 push 自动 cmake + make | CI 绿色 |
| 0.9 | 实现双命名机制（get_version_key_name） | EET 模式生成 `vkey` 列 |
| 0.10 | 移植各 DBMS 的 env_setting_stmts 配置和错误模式正则列表 | PG/YugaByte 查询前注入 SET 语句 |

### Phase 1：EET 模式完整化（~1周）

| 步骤 | 工作内容 | 验证标准 |
|------|----------|----------|
| 1.1 | 移入 EET qcn_tester + eet_main + eet_general_process | 编译通过 |
| 1.2 | 合合 dbms_info（统一选项解析） | 所有 DBMS 参数可解析 |
| 1.3 | 合合 scope（添加 stmt_seq + naming management） | 唯一命名正常 |
| 1.4 | 测试 EET 模式在 MySQL 上运行 | 可生成数据库+测试 |
| 1.5 | 移植假阳性二次验证逻辑（re-validation），确保所有 tester 类型都执行二次验证 | 已知 bug 复现无误报 |
| 1.6 | 移植 DBMS 特定 tester 权重逻辑（ClickHouse choices=6） | ClickHouse 不触发 UPDATE/DELETE tester |
| 1.7 | 测试 EET 模式复现 ≥1 个已知 EET bug | bug 被检测到 |

### Phase 2：TxCheck 模式集成 + DBMS 扩展（~3周）

| 步骤 | 工作内容 | 验证标准 |
|------|----------|----------|
| 2.1 | 移入 txcheck 三大模块（transaction_test, instrumentor, dependency_analyzer） | 编译通过 |
| 2.2 | 创建 tx_general_process（从 TxCheck general_process 拆出专用函数） | 编译通过 |
| 2.3 | 同步修复已知 bug（compare_content 循环、set_intersection 范围） | 修复验证 |
| 2.4 | 合合 grammar.cc（叠加事务语句 + wkey 条件化赋值 + `txn_mode` 约束 + `write_op_id`/`row_id` 计数器） | 事务语句生成正确，无 JOIN/GROUP BY |
| 2.5 | 合合 dut_base（事务方法默认实现 + env_setting_stmts 统一签名） | 编译通过 |
| 2.6 | 合合 mysql.cc（非阻塞 API + env_setting_stmts 统一签名） | MySQL TxCheck 模式可运行 |
| 2.7 | 移入 mariadb.cc/hh | MariaDB TxCheck 模式可运行 |
| 2.8 | 移入 tx_main.cc | `dbfuzz --mode=txcheck --mysql-db=testdb` 可运行 |
| 2.9 | 适配 instrumentor.cc（wkey→get_version_key_name()） | 插桩正确 |
| 2.10 | **P0 验证**：确认 `retry_block_stmt` 递归重试逻辑完整（commit/abort 后递归恢复被阻塞语句） | 阻塞场景下语句恢复率 ≥ 原项目 |
| 2.11 | **P0 验证**：确认 `infer_instrument_after_blocking` 完整移植（队列长度一致性） | `multi_stmt_round_test()` 迭代不崩溃 |
| 2.12 | **P0 验证**：确认多轮状态恢复机制（init_* 快照 + 每轮结束恢复） | 多轮测试结果与单轮独立测试一致 |
| 2.13 | 保留 G2-item / G-SIa / G-SIb 异常检查代码（保持注释状态），确保完整移植 | 代码存在但未启用 |
| 2.14 | 测试 TxCheck 模式复现 ≥1 个已知 TxCheck bug | bug 被检测到 |
| 2.15 | 为 PG/SQLite/其他 DBMS 添加基本 TxCheck 支持（保留 DBMS 特定语法分支） | 各 DBMS 可连接 |

### Phase 3：交叉测试模式（~2周）

| 步骤 | 工作内容 | 验证标准 |
|------|----------|----------|
| 3.1 | 实现 cross_tester：事务流程 + SELECT 类语句 EET 变换 | 编译通过 |
| 3.2 | 实现状态管理流程（backup → 事务执行 → restore → 非事务原始 → restore → 非事务变换 → 三路比较） | 三次执行间状态正确重置 |
| 3.3 | 实现三路比较逻辑（txn vs normal vs transformed-SELECT-only） | 比较正确 |
| 3.4 | **P0 验证**：交叉模式的假阳性二次验证（三路不一致时均重新执行确认） | 误报率 < 5% |
| 3.5 | 实现交叉模式 bug 报告格式 | 输出格式正确 |
| 3.6 | 在 MySQL 上运行交叉模式 | 找到 ≥1 个 bug 或确认无假阳性 |

### Phase 4：完善与优化（持续）

| 步骤 | 工作内容 |
|------|----------|
| 4.1 | 统一测试用例最小化（TxCheck 两级 delta + EET component-based + database minimization） |
| 4.2 | Docker 测试脚本（9 个 DBMS） |
| 4.3 | README + CLAUDE.md |
| 4.4 | 性能基准（各模式吞吐量对比原项目） |
| 4.5 | release_notes.md 版本追踪 |

---

## 12. 关键风险与缓解

| 风险 | 缓解措施 |
|------|----------|
| **grammar.cc 合合复杂** | 分阶段：先 EET 版本跑通，再逐个叠加 TxCheck 特性，每步用已知 bug 回归 |
| **wkey/vkey 命名冲突** | 双命名机制，运行时动态选择 |
| **交叉模式假阳性** | 变换只应用于 SELECT，写入保持原样；**所有三种模式均执行假阳性二次验证（P0-4）** |
| **非阻塞 API 仅 MySQL/MariaDB** | 其他 DBMS 用 timeout 替代阻塞检测 |
| **include 路径改动量大** | 一次性批量替换，CI 编译验证 |
| **CMake 在某些 Linux 上兼容** | 保持 configure.ac 作为备选（可用 AUTOTOLS 双轨制） |
| **G2-item / G-SIa / G-SIb 检查被禁用** | TxCheck 代码中这三种异常检查已完整实现但被注释掉，原因是 VSR 虚假谓词依赖导致误报。短期保持注释状态；长期考虑引入更精确的谓词依赖追踪（仅 SELECT 目标表的行子集而非全表）以减少虚假依赖并重新启用 |
| **printed_expr 循环引用** | 已在 expr 模块中纳入 `printed_expr.cc/hh`（P0-2），确保 CASE 表达式变换不会无限递归 |
| **阻塞调度效率** | 确保 `retry_block_stmt` 的递归重试（P0-5）和 `infer_instrument_after_blocking`（P0-6）完整移植 |

---

## 13. 与原项目兼容性承诺

| 承诺 | 实现 |
|------|------|
| TxCheck 模式 bug 检测能力 ≥ 原 transfuzz | 保留所有核心逻辑，仅接口适配 |
| EET 模式 bug 检测能力 ≥ 原 eet | 保留所有核心逻辑，仅接口适配 |
| 原项目复现脚本可用 | EET 用 vkey 命名不变；提供 `--mode` 等价参数映射 |
| 原项目 bug 报告格式可识别 | 子目录格式与原项目一致 |