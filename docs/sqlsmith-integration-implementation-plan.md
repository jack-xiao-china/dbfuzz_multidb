# sqlsmith 整合实施策略与编码计划（v2）

> 基于 A+B 双轨方案 | 严格复刻 sqlsmith | PostgreSQL 优先 + MySQL 次之
> 日期：2026-06-09

## 目录

- [1. 总体架构](#1-总体架构)
- [2. Track B：Smoke 模式（严格复刻 sqlsmith）](#2-track-bsmoke-模式严格复刻-sqlsmith)
- [3. Track A-1：PostgreSQL 语法补齐](#3-track-a-1postgresql-语法补齐)
- [4. Track A-2：MySQL 语法补齐](#4-track-a-2mysql-语法补齐)
- [5. 依赖关系与实施顺序](#5-依赖关系与实施顺序)
- [6. 测试验证策略](#6-测试验证策略)
- [7. 里程碑与时间线](#7-里程碑与时间线)

---

## 1. 总体架构

### 1.1 设计原则

| 原则 | 说明 |
|------|------|
| **Smoke 零裁剪** | Smoke 模式严格复刻 sqlsmith 原生工具的全部运行时行为，不允许功能裁剪 |
| **PG 优先** | 语法补齐先完成 PostgreSQL，再补齐 MySQL |
| **GaussDB 暂缓** | GaussDB-A/M 暂不要求实现新增语法 |
| **单一代码库** | 所有功能合入 dbfuzz，不维护独立 sqlsmith |

### 1.2 模块变更总览

```
src/
├── core/
│   ├── dbms_info.hh/.cc       [M] 新增 MODE_SMOKE 枚举 + smoke 选项解析
│   ├── relmodel.hh            [M] scope 扩展 (prepared_stmts 列表)
│   ├── impedance.hh/.cc       [M] 对齐 sqlsmith 原生反馈机制
│   └── general_process.hh     [M] 无实质变更
├── grammar/
│   ├── grammar.hh             [M] 新增 data_modifying_cte; group_clause 扩展
│   └── grammar.cc             [M] 实现新 productions; MySQL 语法适配
├── expr/
│   ├── window_function.hh/.cc [M] 新增 window_frame
│   ├── named_window.hh/.cc    [M] 同步帧子句
│   ├── bool_expr/
│   │   ├── quantified_comparison.hh/.cc  [N] 量化比较
│   │   └── bool_expr.cc       [M] factory 新增分支
│   ├── json_expr.hh/.cc       [N] JSON 运算符 (PG)
│   ├── array_expr.hh/.cc      [N] 数组构造 (PG)
│   ├── row_constructor.hh/.cc  [N] ROW 构造 (PG)
│   └── expr.hh                [M] 新增 include
├── schema/
│   ├── schema.hh              [M] feature_flags + MySQL 类型扩展
│   ├── schema.cc              [M] 索引生成扩展
│   ├── postgres.cc            [M] PG feature flags 设置
│   └── mysql.cc               [M] 反引号、函数提取、ON DUPLICATE KEY 等
├── smoke/                     [N] 新模块
│   ├── smoke_main.hh          [N] 入口 + 配置结构体
│   ├── smoke_run.cc           [N] 主循环（严格复刻 sqlsmith）
│   └── smoke_logger.hh/.cc    [N] 日志器体系（复刻 sqlsmith logger）
├── main.cc                    [M] 新增 smoke 模式分发
└── CMakeLists.txt             [M] 新增 smoke_objs
```

标记：`[M]` = 修改, `[N]` = 新增

### 1.3 Feature Flags 机制

为避免在不支持的 DBMS 上生成不兼容语法，引入 feature flag 机制：

```cpp
// schema/schema.hh — schema 类新增
struct db_feature_flags {
    // ── PostgreSQL 特性 ──
    bool has_window_frame    = false;
    bool has_data_mod_cte    = false;
    bool has_quantified_cmp  = false;
    bool has_grouping_sets   = false;
    bool has_json_jsonb      = false;
    bool has_array_ops       = false;
    bool has_merge           = false;  // PG 15+
    bool has_upsert          = false;  // PG 9.5+
    bool has_returning       = false;
    bool has_tablesample     = false;  // PG 9.5+
    bool has_lateral         = false;
    bool has_for_update      = false;

    // ── MySQL 特性 ──
    bool has_on_duplicate_key = false;
    bool has_backtick_quote   = false;
    bool has_if_function      = false;
    bool has_group_concat     = false;
    bool has_limit_offset_alt = false; // LIMIT offset, count

    // ── 通用 ──
    bool has_full_outer_join = false;
    bool has_intersect_except = false;
};

db_feature_flags features;
```

```cpp
// schema/postgres.cc — 设置
features.has_window_frame    = true;
features.has_data_mod_cte    = true;
features.has_quantified_cmp  = true;
features.has_grouping_sets   = true;
features.has_json_jsonb      = true;
features.has_array_ops       = true;
features.has_upsert          = true;
features.has_returning       = true;
features.has_tablesample     = true;
features.has_lateral         = true;
features.has_for_update      = true;
features.has_full_outer_join = true;
features.has_intersect_except = true;

// schema/mysql.cc — 设置
features.has_on_duplicate_key = true;
features.has_backtick_quote   = true;
features.has_if_function      = true;
features.has_group_concat     = true;
features.has_limit_offset_alt = true;
// MySQL 不支持的特性保持 false
```

grammar 生成时检查：
```cpp
if (!scope->schema->features.has_data_mod_cte)
    fail("not supported by this DBMS");
```

---

## 2. Track B：Smoke 模式（严格复刻 sqlsmith）

### 2.1 sqlsmith 原生行为精确规格

以下是 sqlsmith 运行时的完整行为契约，Smoke 模式必须逐项满足：

#### 2.1.1 事务包裹模式

```
每条语句执行序列：
  ROLLBACK;     ← 清理上一轮可能残留的事务状态
  BEGIN;        ← 开启新事务
  <stmt>;       ← 执行生成的 SQL
  ROLLBACK;     ← 永远不 COMMIT，丢弃所有变更
```

**关键约束：**
- 每条语句独立事务
- 永远 ROLLBACK，永不 COMMIT
- 首条 ROLLBACK 清理残留状态（防止前一条语句异常退出后的脏事务）

#### 2.1.2 会话变量

每条新连接建立后立即设置：
```sql
SET statement_timeout TO '1s';
SET client_min_messages TO 'ERROR';
SET application_name TO 'dbfuzz::smoke';
```

#### 2.1.3 错误分类体系

sqlsmith 使用三级异常层次结构：

| 异常类型 | 触发条件 | 处理方式 |
|----------|---------|---------|
| `dut::broken` | 连接断开（`PQstatus != CONNECTION_OK`、`broken_connection`） | 外层循环捕获，sleep 1s，重连 |
| `dut::timeout` | 匹配 `"canceling statement due to statement timeout"` | 记录为 timeout，impedance 负反馈 |
| `dut::syntax` | SQLSTATE `42601` 或匹配 `"syntax error at or near"` | 记录为语法错误，impedance 负反馈 |
| `dut::failure` | 所有其他 SQL 错误 | 记录为潜在 Bug，impedance 负反馈 |

**连接恢复：** 外层 while(1) 循环捕获 `dut::broken`，sleep 1 秒后自动重连，不退出程序。

#### 2.1.4 阻抗反馈机制（impedance feedback）

sqlsmith 的核心智能：追踪每种 AST 节点类型的成功/失败率，自动黑名单化高失败率的 production。

**数据结构：**
```cpp
// 全局统计（按 typeid(*prod).name() 索引）
map<const char*, long> occurances_in_failed_query;
map<const char*, long> occurances_in_ok_query;
map<const char*, long> retries;
map<const char*, long> limited;
map<const char*, long> failed;
```

**visitor 机制：**
```cpp
class impedance_visitor {
    map<const char*, long> &_occured;
    map<string, bool> found;  // 去重：每种类型在当前查询中只计一次

    void visit(struct prod *p) {
        found[typeid(*p).name()] = true;
    }

    ~impedance_visitor() {
        for (auto pair : found)
            _occured[pair.first]++;
    }
};
```

**反馈时机：**
- `executed(prod &query)` → 语句成功执行 → 遍历 AST 所有节点，递增 `occurances_in_ok_query`
- `error(prod &query, dut::failure &e)` → 语句执行出错 → 遍历 AST 所有节点，递增 `occurances_in_failed_query`

**黑名单判定：**
```cpp
bool impedance::matched(const char *name) {
    // 至少 100 次失败才考虑黑名单
    if (100 > occurances_in_failed_query[name])
        return true;  // 样本不足，不黑名单

    double error_rate = (double)occurances_in_failed_query[name]
        / (occurances_in_failed_query[name] + occurances_in_ok_query[name]);

    if (error_rate > 0.99)
        return false;  // 黑名单：>99% 错误率

    return true;
}
```

**其他追踪：**
- `retry(const char *p)` → production 构造重试 → 递增 `retries`
- `limit(const char *p)` → 重试次数超限 → 递增 `limited`
- `fail(const char *p)` → production 构造失败 → 递增 `failed`

#### 2.1.5 语句生成分发概率（statement_factory）

sqlsmith 的精确概率分布（Smoke 模式必须完全复用）：

| 语句类型 | 概率 | 判定条件 |
|----------|------|---------|
| `merge_stmt` | ~2.4% | `d42() == 1`（第一优先） |
| `insert_stmt` | ~2.4% | `d42() == 1` |
| `delete_returning` | ~2.4% | `d42() == 1` |
| `upsert_stmt` | ~2.4% | `d42() == 1` |
| `update_returning` | ~2.4% | `d42() == 1` |
| `select_for_update` | ~33.3% | `d6() > 4` |
| `common_table_expression` | ~16.7% | `d6() > 5` |
| `query_spec`（纯 SELECT） | ~38% | 默认 |

**注意：** 这些概率是有条件级联的——每个 d42() 独立判定，只有前面的都未命中才会判定后续。

#### 2.1.6 查询子句概率

| 子句 | 概率 | 判定 |
|------|------|------|
| DISTINCT | 1% | `d100() == 1` |
| LIMIT | 66.7% | `d6() > 2`，值为 `d100()+d100()` (2-200) |
| LATERAL 子查询追加 | 16.7% | `d6() > 5`（在 FROM 子句中） |

#### 2.1.7 表达式生成概率（value_expr::factory）

| 表达式类型 | 概率 | 前置条件 |
|-----------|------|---------|
| `window_function` | 5% | `d20()==1 && level<d6() && allowed()` |
| `coalesce` | ~2.4% | `d42()==1 && level<d6()` |
| `nullif` | ~2.4% | `d42()==1 && level<d6()` |
| `funcall` | ~16.7% | `d6()==1 && level<d6()` |
| `atomic_subselect` | 8.3% | `d12()==1` |
| `case_expr` | ~11.1% | `d9()==1 && level<d6()` |
| `column_reference` | ~95% | `refs.size()>0 && d20()>1` |
| `const_expr` | 默认 | 回退 |

#### 2.1.8 布尔表达式概率（bool_expr::factory）

| 表达式类型 | 概率 | 判定 |
|-----------|------|------|
| `truth_value`（短路） | 按 level | `level > d100()` |
| `comparison_op` | 50% | `d6() < 4` |
| `bool_term` (AND/OR) | 50%（剩余） | `d6() < 4` |
| `null_predicate` | 50%（剩余） | `d6() < 4` |
| `truth_value` | 50%（剩余） | `d6() < 4` |
| `exists_predicate` | 默认 | 剩余 |

#### 2.1.9 重试机制

```
每个 prod 实例有 retries 计数器（初始 0）
retry_limit = 100（table_sample 为 1000）

retry() 逻辑：
  1. 调用 impedance::retry(this) 追踪
  2. retries++
  3. 若 retries > retry_limit → impedance::limit(this) → throw runtime_error
  4. 否则 return（调用者重新尝试生成）
```

#### 2.1.10 CLI 选项（sqlsmith 原生）

| 选项 | 说明 | 默认值 |
|------|------|--------|
| `--target=connstr` | PG 连接串 | 无（必须指定） |
| `--seed=int` | 随机种子 | `getpid()` |
| `--max-queries=N` | 最大查询数 | 0（无限） |
| `--verbose` | 打印进度到 stderr | 关闭 |
| `--dump-all-queries` | 打印每条生成的 SQL | 关闭 |
| `--dump-all-graphs` | 打印 AST 结构 | 关闭 |
| `--dry-run` | 只生成不执行 | 关闭 |
| `--exclude-catalog` | 跳过 pg_catalog 表 | 关闭 |
| `--log-to=connstr` | 将错误日志写入 PG 数据库 | 关闭 |
| `--rng-state=string` | 反序列化 RNG 状态 | 无 |

#### 2.1.11 信号处理

- 注册 `SIGINT` 处理（仅在 `--verbose` 模式下）
- 收到 SIGINT → 打印阻抗反馈报告（JSON 格式）→ exit(1)

#### 2.1.12 日志器体系

sqlsmith 使用观察者模式的 logger 链：

```cpp
struct logger {
    virtual void generated(prod &query);          // AST 生成后
    virtual void executed(prod &query);            // 执行成功后
    virtual void error(prod &query, dut::failure &e);  // 执行失败后
};
```

内置 logger：
1. `impedance_feedback` — 阻抗反馈追踪
2. `cerr_logger` — stderr 进度输出（`--verbose`）
3. `query_dumper` — SQL 文本输出（`--dump-all-queries`）
4. `ast_logger` — AST 结构输出（`--dump-all-graphs`）
5. `pqxx_logger` — 错误日志写入 PG（`--log-to`）

---

### 2.2 Smoke 模式模块设计

#### 2.2.1 文件结构

```
src/smoke/
├── smoke_main.hh        入口声明 + smoke_options
├── smoke_run.cc         主循环（复刻 sqlsmith.cc 主循环）
├── smoke_logger.hh      logger 基类 + 所有内置 logger 声明
└── smoke_logger.cc      logger 实现（impedance_feedback + cerr + dumper）
```

#### 2.2.2 smoke_main.hh

```cpp
#pragma once
#include "config.h"
#include <string>
#include <map>
#include "core/dbms_info.hh"

// ── Smoke 模式选项（对齐 sqlsmith CLI） ──
struct smoke_options {
    int seed = 0;                   // 随机种子，0 = getpid()
    int max_queries = 0;            // 最大查询数，0 = 无限
    bool verbose = false;           // --verbose
    bool dump_all_queries = false;  // --dump-all-queries
    bool dump_all_graphs = false;   // --dump-all-graphs
    bool dry_run = false;           // --dry-run
    bool exclude_catalog = false;   // --exclude-catalog
    string log_to;                  // --log-to=connstr（PG 连接串）
    string rng_state;               // --rng-state（反序列化 RNG 状态）
};

void smoke_run(dbms_info &d_info, std::map<std::string, std::string> &options);
```

#### 2.2.3 smoke_logger.hh

```cpp
#pragma once
#include "core/prod.hh"
#include "schema/dut.hh"
#include <iostream>
#include <map>
#include <string>

// ── 异常层次（复刻 sqlsmith dut::failure/broken/timeout/syntax）──
namespace smoke_dut {
    struct failure : std::exception {
        std::string errstr;
        std::string sqlstate;
        failure(const std::string &e, const std::string &s = "")
            : errstr(e), sqlstate(s) {}
        const char* what() const noexcept override { return errstr.c_str(); }
    };
    struct broken : failure {
        using failure::failure;
    };
    struct timeout : failure {
        using failure::failure;
    };
    struct syntax : failure {
        using failure::failure;
    };
}

// ── Logger 基类（复刻 sqlsmith logger 接口）──
struct smoke_logger {
    virtual void generated(prod &query) {}
    virtual void executed(prod &query) {}
    virtual void error(prod &query, const smoke_dut::failure &e) {}
    virtual void report() {}
    virtual ~smoke_logger() {}
};

// ── 阻抗反馈 Logger（复刻 sqlsmith impedance_feedback）──
struct impedance_feedback_logger : smoke_logger {
    void executed(prod &query) override;
    void error(prod &query, const smoke_dut::failure &e) override;
    void report() override;  // 输出 JSON 报告
};

// ── stderr 进度 Logger（复刻 sqlsmith cerr_logger）──
struct cerr_logger : smoke_logger {
    unsigned long long generated_count = 0;
    unsigned long long executed_count = 0;
    unsigned long long error_count = 0;
    unsigned long long broken_count = 0;

    void generated(prod &query) override;
    void executed(prod &query) override;
    void error(prod &query, const smoke_dut::failure &e) override;
    void report() override;
};

// ── SQL 输出 Logger（复刻 sqlsmith query_dumper）──
struct query_dumper_logger : smoke_logger {
    void generated(prod &query) override;
};

// ── AST 结构输出 Logger（复刻 sqlsmith ast_logger）──
struct ast_dumper_logger : smoke_logger {
    void generated(prod &query) override;
};

// ── 阻抗反馈全局函数（复刻 sqlsmith impedance 命名空间）──
namespace smoke_impedance {
    bool matched(const char *name);
    void retry(const char *name);
    void limit(const char *name);
    void fail(const char *name);
    void report(std::ostream &out);
}
```

#### 2.2.4 smoke_run.cc 主循环（严格复刻 sqlsmith.cc）

```cpp
#include "config.h"
#include "smoke_main.hh"
#include "smoke_logger.hh"
#include "core/general_process.hh"
#include "core/relmodel.hh"
#include "core/impedance.hh"
#include "grammar/grammar.hh"
#include "schema/schema.hh"
#include "schema/dut.hh"
#include <iostream>
#include <fstream>
#include <csignal>
#include <chrono>
#include <thread>

using namespace std;

// ── 全局状态（复刻 sqlsmith 全局变量模式）──
static cerr_logger *global_cerr_logger = nullptr;
static vector<shared_ptr<smoke_logger>> loggers;

// 信号处理（复刻 sqlsmith cerr_log_handler）
extern "C" void smoke_signal_handler(int) {
    if (global_cerr_logger)
        global_cerr_logger->report();
    // 输出阻抗反馈报告
    smoke_impedance::report(cerr);
    exit(1);
}

// ── 将 dbfuzz DUT 异常映射为 sqlsmith 风格异常 ──
static void translate_dut_error(const string &err) {
    // 连接断开检测
    if (err.find("connection") != string::npos ||
        err.find("server closed") != string::npos ||
        err.find("broken pipe") != string::npos ||
        err.find("Connection refused") != string::npos ||
        err.find("CONNECTION_BAD") != string::npos ||
        err.find("Lost connection") != string::npos ||
        err.find("MySQL server has gone") != string::npos) {
        throw smoke_dut::broken(err);
    }

    // 超时检测
    if (err.find("statement timeout") != string::npos ||
        err.find("canceling statement") != string::npos ||
        err.find("Query execution was interrupted") != string::npos) {
        throw smoke_dut::timeout(err);
    }

    // 语法错误检测
    if (err.find("syntax error") != string::npos ||
        err.find("SQL syntax") != string::npos ||
        err.find("You have an error in your SQL") != string::npos) {
        throw smoke_dut::syntax(err);
    }

    // 其他错误
    throw smoke_dut::failure(err);
}

// ═══════════════════════════════════════════════════
// 主入口
// ═══════════════════════════════════════════════════

void smoke_run(dbms_info &d_info, map<string, string> &options)
{
    // ── 1. 解析选项（对齐 sqlsmith CLI） ──
    smoke_options opts;
    if (options.count("seed"))
        opts.seed = stoi(options["seed"]);
    if (options.count("max-queries"))
        opts.max_queries = stoi(options["max-queries"]);
    if (options.count("verbose"))
        opts.verbose = true;
    if (options.count("dump-all-queries"))
        opts.dump_all_queries = true;
    if (options.count("dump-all-graphs"))
        opts.dump_all_graphs = true;
    if (options.count("dry-run"))
        opts.dry_run = true;
    if (options.count("exclude-catalog"))
        opts.exclude_catalog = true;
    if (options.count("log-to"))
        opts.log_to = options["log-to"];
    if (options.count("rng-state"))
        opts.rng_state = options["rng-state"];

    // ── 2. 设置日志器（复刻 sqlsmith logger 链） ──
    // 阻抗反馈 logger 始终启用
    loggers.push_back(make_shared<impedance_feedback_logger>());

    if (opts.verbose) {
        auto l = make_shared<cerr_logger>();
        global_cerr_logger = &*l;
        loggers.push_back(l);
        signal(SIGINT, smoke_signal_handler);  // 仅 verbose 时注册
    }
    if (opts.dump_all_queries)
        loggers.push_back(make_shared<query_dumper_logger>());
    if (opts.dump_all_graphs)
        loggers.push_back(make_shared<ast_dumper_logger>());

    // ── 3. 初始化 DUT 和 Schema ──
    shared_ptr<dut_base> conn;
    shared_ptr<schema> db_schema;

    try {
        conn = dut_setup(d_info);
        db_schema = get_schema(d_info);
    } catch (const exception &e) {
        cerr << "[SMOKE] Failed to initialize: " << e.what() << endl;
        return;
    }

    if (opts.verbose) {
        cerr << "[SMOKE] Schema: " << db_schema->tables.size() << " tables, "
             << db_schema->ops.size() << " operators, "
             << db_schema->routines.size() << " routines, "
             << db_schema->aggregates.size() << " aggregates" << endl;
    }

    // ── 4. 设置会话变量（复刻 sqlsmith connect()） ──
    if (!opts.dry_run) {
        try {
            // PostgreSQL 使用 libpq 直接设置
            conn->test("SET statement_timeout TO '1s'", "");
            conn->test("SET client_min_messages TO 'ERROR'", "");
            conn->test("SET application_name TO 'dbfuzz::smoke'", "");
        } catch (...) {
            // MySQL 等不支持 statement_timeout 的 DBMS，静默跳过
            if (opts.verbose)
                cerr << "[SMOKE] Warning: could not set session variables" << endl;
        }
    }

    // ── 5. 构建 scope（复刻 sqlsmith main() scope 初始化） ──
    struct scope root_scope;
    root_scope.schema = db_schema.get();
    root_scope.stmt_seq = make_shared<map<string, unsigned int>>();

    for (auto &t : db_schema->tables) {
        if (opts.exclude_catalog &&
            (t->schema == "pg_catalog" || t->schema == "information_schema"))
            continue;
        root_scope.tables.push_back(t.get());
    }
    // 填充 refs（所有表的所有列）
    for (auto &t : root_scope.tables) {
        for (auto &c : t->columns()) {
            root_scope.refs.push_back(t);
            break;  // 每个表只添加一次
        }
    }

    // ── 6. 主循环（严格复刻 sqlsmith 双层循环结构） ──
    unsigned long long query_count = 0;

    while (1) {  // 外层：连接恢复循环
        try {
            while (1) {  // 内层：查询生成循环

                // 检查 max-queries
                if (opts.max_queries > 0 &&
                    query_count >= (unsigned long long)opts.max_queries) {
                    // 输出最终报告
                    for (auto l : loggers) l->report();
                    return;
                }

                // ── 生成 AST ──
                shared_ptr<prod> gen;
                try {
                    root_scope.new_stmt();
                    gen = statement_factory(&root_scope);
                } catch (const runtime_error &e) {
                    // 生成失败，重试
                    continue;
                }

                // 通知 loggers: generated
                for (auto l : loggers) l->generated(*gen);

                if (opts.dry_run) {
                    // dry-run 模式：只输出不执行
                    query_count++;
                    continue;
                }

                // ── 转换为 SQL 字符串 ──
                ostringstream sql;
                sql << *gen;
                string sql_str = sql.str();

                // ── 执行（复刻 sqlsmith 事务包裹） ──
                try {
                    // ROLLBACK → BEGIN → stmt → ROLLBACK
                    try { conn->test("ROLLBACK", ""); } catch (...) {}
                    conn->test("BEGIN", "");
                    conn->test(sql_str, "");
                    conn->test("ROLLBACK", "");

                    // 成功 → 通知 loggers: executed
                    for (auto l : loggers) l->executed(*gen);

                } catch (const exception &e) {
                    // 翻译为 sqlsmith 风格异常
                    try {
                        translate_dut_error(e.what());
                    } catch (const smoke_dut::broken &be) {
                        // 连接断开 → 重抛到外层
                        for (auto l : loggers) {
                            try { l->error(*gen, be); } catch (...) {}
                        }
                        throw be;  // 重抛到外层循环
                    } catch (const smoke_dut::failure &fe) {
                        // 其他错误 → 通知 loggers
                        for (auto l : loggers) {
                            try { l->error(*gen, fe); } catch (...) {}
                        }
                        // 尝试 ROLLBACK 清理
                        try { conn->test("ROLLBACK", ""); } catch (...) {}
                    }
                }

                query_count++;
            }
        }
        catch (const smoke_dut::broken &e) {
            // ── 连接恢复（复刻 sqlsmith 外层循环） ──
            if (opts.verbose)
                cerr << "[SMOKE] Connection lost: " << e.what()
                     << " — reconnecting in 1s..." << endl;

            this_thread::sleep_for(chrono::milliseconds(1000));

            // 重连
            try {
                conn = dut_setup(d_info);
                // 重新设置会话变量
                try {
                    conn->test("SET statement_timeout TO '1s'", "");
                    conn->test("SET client_min_messages TO 'ERROR'", "");
                    conn->test("SET application_name TO 'dbfuzz::smoke'", "");
                } catch (...) {}
            } catch (const exception &re) {
                if (opts.verbose)
                    cerr << "[SMOKE] Reconnect failed: " << re.what() << endl;
                // 继续外层循环重试
            }
        }
    }
}
```

#### 2.2.5 smoke_logger.cc — 阻抗反馈实现

```cpp
#include "smoke_logger.hh"
#include "core/prod.hh"
#include <typeinfo>
#include <map>

using namespace std;

// ── 全局阻抗统计（复刻 sqlsmith impedance.cc 全局变量）──
static map<const char*, long> occurances_in_failed_query;
static map<const char*, long> occurances_in_ok_query;
static map<const char*, long> retry_counts;
static map<const char*, long> limited_counts;
static map<const char*, long> failed_counts;

// ── 阻抗 visitor（复刻 sqlsmith impedance_visitor）──
class impedance_visitor : public prod_visitor {
    map<const char*, long> &_occured;
    map<string, bool> found;

public:
    impedance_visitor(map<const char*, long> &occured) : _occured(occured) {}

    void visit(struct prod *p) override {
        found[typeid(*p).name()] = true;
    }

    ~impedance_visitor() {
        for (auto &pair : found)
            _occured[pair.first.c_str()]++;
    }
};

// ── impedance_feedback_logger ──
void impedance_feedback_logger::executed(prod &query) {
    impedance_visitor v(occurances_in_ok_query);
    query.accept(&v);
}

void impedance_feedback_logger::error(prod &query, const smoke_dut::failure &e) {
    impedance_visitor v(occurances_in_failed_query);
    query.accept(&v);
}

void impedance_feedback_logger::report() {
    smoke_impedance::report(cerr);
}

// ── cerr_logger ──
void cerr_logger::generated(prod &query) {
    generated_count++;
    if (generated_count % 1000 == 0) {
        cerr << "\r[SMOKE] gen: " << generated_count
             << " ok: " << executed_count
             << " err: " << error_count
             << " broken: " << broken_count;
    }
}

void cerr_logger::executed(prod &query) {
    executed_count++;
}

void cerr_logger::error(prod &query, const smoke_dut::failure &e) {
    error_count++;
    if (dynamic_cast<const smoke_dut::broken*>(&e))
        broken_count++;
}

void cerr_logger::report() {
    cerr << "\n[SMOKE] === Final Report ===" << endl;
    cerr << "  Generated: " << generated_count << endl;
    cerr << "  Executed:  " << executed_count << endl;
    cerr << "  Errors:    " << error_count << endl;
    cerr << "  Broken:    " << broken_count << endl;
}

// ── query_dumper_logger ──
void query_dumper_logger::generated(prod &query) {
    ostringstream os;
    os << query;
    cout << os.str() << ";" << endl;
}

// ── 阻抗命名空间函数 ──
namespace smoke_impedance {

bool matched(const char *name) {
    if (100 > occurances_in_failed_query[name])
        return true;
    double error_rate = (double)occurances_in_failed_query[name]
        / (occurances_in_failed_query[name] + occurances_in_ok_query[name]);
    if (error_rate > 0.99)
        return false;
    return true;
}

void retry(const char *name) { retry_counts[name]++; }
void limit(const char *name) { limited_counts[name]++; }
void fail(const char *name) { failed_counts[name]++; }

void report(std::ostream &out) {
    out << "{\"impedance\": [" << endl;
    bool first = true;
    for (auto &pair : occurances_in_failed_query) {
        if (!first) out << "," << endl;
        first = false;
        out << "{\"prod\": \"" << pair.first << "\","
            << "\"bad\": " << pair.second << ","
            << "\"ok\": " << occurances_in_ok_query[pair.first] << ","
            << "\"limited\": " << limited_counts[pair.first] << ","
            << "\"failed\": " << failed_counts[pair.first] << ","
            << "\"retries\": " << retry_counts[pair.first] << "}";
    }
    out << endl << "]}" << endl;
}

} // namespace smoke_impedance
```

#### 2.2.6 与 sqlsmith 行为对齐检查表

| sqlsmith 行为 | Smoke 模式实现 | 对齐状态 |
|---------------|--------------|---------|
| `ROLLBACK; BEGIN; stmt; ROLLBACK;` | smoke_run.cc 主循环 | ✅ |
| `SET statement_timeout TO '1s'` | connect 后设置 | ✅ |
| `SET client_min_messages TO 'ERROR'` | connect 后设置 | ✅ |
| 连接断开 → sleep 1s → 重连 | 外层 while(1) 捕获 broken | ✅ |
| 阻抗反馈 executed/error | impedance_feedback_logger | ✅ |
| 黑名单: 100+ failures AND >99% | smoke_impedance::matched() | ✅ |
| 信号处理: SIGINT → report → exit | smoke_signal_handler | ✅ |
| --verbose 进度输出 | cerr_logger | ✅ |
| --dump-all-queries | query_dumper_logger | ✅ |
| --dump-all-graphs | ast_dumper_logger | ✅ |
| --dry-run 只生成不执行 | dry_run 分支 | ✅ |
| --exclude-catalog | scope 初始化过滤 | ✅ |
| --max-queries | query_count 检查 | ✅ |
| --seed 随机种子 | 复用 dbfuzz 的 seed 机制 | ✅ |
| --log-to PG 日志 | （后续 Sprint 实现） | ⏳ |
| statement_factory 概率分布 | 复用 dbfuzz 现有 factory | ✅ |
| 重试机制 retry_limit=100 | 复用 dbfuzz prod::retry() | ✅ |
| impedance::matched 在 grammar 中调用 | 需确认 dbfuzz 现有调用链 | ⚠️ 待验证 |

#### 2.2.7 需要验证的 dbfuzz 阻抗反馈集成

dbfuzz 已有 `impedance.cc`（`src/core/impedance.cc`），需要验证：
- `impedance::matched()` 是否在 grammar 生成中被调用
- 是否与 sqlsmith 的调用点一致
- 如果 dbfuzz 已有完整的阻抗反馈，Smoke 模式应直接复用，不需要重新实现

**如果 dbfuzz 的阻抗反馈与 sqlsmith 等价**：Smoke 模式直接使用 dbfuzz 现有的 `impedance` 命名空间函数，`smoke_logger.cc` 中的 `smoke_impedance` 改为调用 `impedance::matched()` 等。

**如果 dbfuzz 的阻抗反馈有差异**：在 `smoke_logger.cc` 中独立实现，确保行为完全对齐。

#### 2.2.8 CLI 选项映射

| sqlsmith 选项 | dbfuzz Smoke 选项 | 说明 |
|---------------|-------------------|------|
| `--target=connstr` | `--postgres-db=X --postgres-port=Y ...` | dbfuzz 使用分离参数 |
| `--seed=N` | `--seed=N` | 直接复用 |
| `--max-queries=N` | `--max-queries=N` | 对齐命名 |
| `--verbose` | `--verbose` | 对齐命名 |
| `--dump-all-queries` | `--dump-all-queries` | 对齐命名 |
| `--dump-all-graphs` | `--dump-all-graphs` | 对齐命名 |
| `--dry-run` | `--dry-run` | 对齐命名 |
| `--exclude-catalog` | `--exclude-catalog` | 对齐命名 |
| `--log-to=connstr` | `--log-to=connstr` | 对齐命名 |
| `--rng-state=str` | `--rng-state=str` | 对齐命名 |
| （无） | `--mode=smoke` | dbfuzz 模式选择 |

#### 2.2.9 Smoke 使用示例

```bash
# 基础：复刻 sqlsmith 默认行为
./build/dbfuzz --mode=smoke --verbose \
    --postgres-db=testdb --postgres-port=5432 \
    --postgres-host=localhost --postgres-user=tpcc --postgres-pass=your_password

# 限制查询数
./build/dbfuzz --mode=smoke --verbose --max-queries=10000 \
    --postgres-db=testdb --postgres-port=5432

# dry-run：只生成 SQL 不执行（调试用）
./build/dbfuzz --mode=smoke --dry-run --dump-all-queries \
    --postgres-db=testdb --postgres-port=5432

# 跳过 catalog 表
./build/dbfuzz --mode=smoke --verbose --exclude-catalog \
    --postgres-db=testdb --postgres-port=5432

# MySQL Smoke（statement_timeout 自动跳过）
./build/dbfuzz --mode=smoke --verbose \
    --mysql-db=testdb --mysql-port=3306
```

---

## 3. Track A-1：PostgreSQL 语法补齐

### 3.1 P0-1：窗口帧子句

**目标 SQL：**
```sql
SUM(x) OVER (PARTITION BY col ORDER BY col ROWS BETWEEN 2 PRECEDING AND CURRENT ROW)
```

**修改文件：**
- `src/expr/window_function.hh` — 新增 `window_frame` 结构体
- `src/expr/window_function.cc` — 帧生成和输出
- `src/grammar/grammar.hh` — `named_window` 同步帧支持
- `src/grammar/grammar.cc` — `named_window` 帧生成

**数据结构：**

```cpp
// window_function.hh
struct window_frame : prod {
    enum frame_mode { ROWS_MODE, RANGE_MODE, GROUPS_MODE };
    enum boundary_type {
        UNBOUNDED_PRECEDING, N_PRECEDING, CURRENT_ROW,
        N_FOLLOWING, UNBOUNDED_FOLLOWING
    };
    frame_mode mode;
    boundary_type start_bound;
    int start_offset;
    boundary_type end_bound;
    int end_offset;

    window_frame(prod *p);
    virtual void out(std::ostream &out);
    virtual void accept(prod_visitor *v);
};

// window_function 类新增成员
struct window_function : value_expr {
    // ... 现有成员 ...
    shared_ptr<window_frame> frame;  // 新增
};
```

**生成逻辑：**
- `frame` 在 `d6() > 3`（66.7%）时生成
- 模式选择：ROWS(50%) / RANGE(33%) / GROUPS(17%)
- 起始边界：UNBOUNDED_PRECEDING(17%) / N_PRECEDING(33%) / CURRENT_ROW(50%)
- 结束边界：CURRENT_ROW(17%) / N_FOLLOWING(33%) / UNBOUNDED_FOLLOWING(50%)
- 需检查 `scope->schema->features.has_window_frame`

**EET 兼容性：** 帧子句不参与 QCN 等价变换（帧范围改变导致结果不同），`equivalent_transform()` 无需修改。

**工期：3 人天**

### 3.2 P0-2：数据修改 CTE

**目标 SQL：**
```sql
WITH moved AS (DELETE FROM t WHERE cond RETURNING *)
INSERT INTO t2 SELECT * FROM moved;
```

**修改文件：**
- `src/grammar/grammar.hh` — 新增 `cte_dml_item` + `data_modifying_cte`
- `src/grammar/grammar.cc` — 实现
- `src/grammar/grammar.cc` — statement_factory 新增分支
- `src/txcheck/instrumentor.cc` — CTE DML 识别（TxCheck 兼容）

**数据结构：**

```cpp
struct cte_dml_item : prod {
    enum dml_type { CTE_INSERT, CTE_UPDATE, CTE_DELETE };
    dml_type type;
    shared_ptr<prod> dml_stmt;
    table *victim;
    struct scope myscope;
    cte_dml_item(prod *p, struct scope *s, table *v);
    virtual void out(std::ostream &out);
};

struct data_modifying_cte : prod {
    vector<shared_ptr<cte_dml_item>> cte_items;
    vector<shared_ptr<named_relation>> cte_refs;
    shared_ptr<query_spec> final_query;
    struct scope myscope;
    data_modifying_cte(prod *p, struct scope *s, bool txn_mode = false);
    virtual void out(std::ostream &out);
};
```

**关键约束：**
- PostgreSQL 要求 CTE 中的 DML 必须有 RETURNING 子句
- CTE 别名在后续 CTE 和最终查询中作为表引用可见
- 需检查 `scope->schema->features.has_data_mod_cte`
- statement_factory 中 `d42() == 1` 概率生成

**TxCheck 兼容：** instrumentor 需识别 CTE 内 DML，按 INSERT_WRITE/UPDATE_WRITE/DELETE_WRITE 分类。

**工期：5 人天 + 2 人天（instrumentor 适配）= 7 人天**

### 3.3 P1-1：量化比较 (ALL/ANY/SOME)

**目标 SQL：**
```sql
x > ALL (SELECT col FROM t)
x = ANY (SELECT col FROM t)
```

**新增文件：**
- `src/expr/bool_expr/quantified_comparison.hh/.cc`

**修改文件：**
- `src/expr/bool_expr/bool_expr.cc` — factory 新增分支（`d20()==1` 时尝试生成）

**数据结构：**

```cpp
struct quantified_comparison : bool_expr {
    enum quantifier { ALL, ANY, SOME };
    string comp_op;
    quantifier quant;
    shared_ptr<value_expr> lhs;
    shared_ptr<query_spec> subquery;
    struct scope myscope;
    quantified_comparison(prod *p, struct scope *s);
    virtual void out(std::ostream &out);
};
```

**工期：2 人天**

### 3.4 P1-2：GROUPING SETS / CUBE / ROLLUP

**目标 SQL：**
```sql
GROUP BY GROUPING SETS ((a,b), (a), ())
GROUP BY CUBE (a, b, c)
GROUP BY ROLLUP (a, b)
```

**修改文件：**
- `src/grammar/grammar.hh` — `group_clause` 新增 `group_type` 枚举和多集成员
- `src/grammar/grammar.cc` — 构造函数和输出扩展

**数据结构扩展：**

```cpp
struct group_clause : prod {
    enum group_type { GROUP_SIMPLE, GROUP_GROUPING_SETS, GROUP_CUBE, GROUP_ROLLUP };
    group_type type;
    // ... 现有成员 ...
    vector<vector<shared_ptr<column_reference>>> group_sets;  // GROUPING SETS
    vector<shared_ptr<column_reference>> cube_rollup_cols;    // CUBE/ROLLUP
};
```

**概率：** 当 `has_grouping_sets && cols>=2` 时：SIMPLE(33%) / GROUPING_SETS(17%) / CUBE(17%) / ROLLUP(33%)

**工期：2 人天**

### 3.5 P1-3：JSON/JSONB 操作

**阶段 1（P1）：** `->` / `->>` 运算符 + JSON 函数调用
**阶段 2（P2）：** `#>` / `#>>` 路径运算符 + JSON Path

**新增文件：**
- `src/expr/json_expr.hh/.cc` — `json_extract_op` 类

**修改文件：**
- `src/schema/schema.hh` — 新增 `jsonbtype` 类型指针
- `src/schema/postgres.cc` — 识别 jsonb 类型
- `src/expr/value_expr.hh` — factory 新增 JSON 分支

**数据结构：**

```cpp
struct json_extract_op : value_expr {
    enum op_type { JSON_ARROW, JSON_DOUBLE_ARROW, JSON_HASH_ARROW, JSON_HASH_DOUBLE };
    op_type op;
    shared_ptr<value_expr> lhs;
    string key_or_path;
    json_extract_op(prod *p, sqltype *type_constraint);
    virtual void out(std::ostream &out);
};
```

**工期：5 人天（阶段 1） + 3 人天（阶段 2）= 8 人天**

### 3.6 P2-P3 特性

| 特性 | 新增/修改文件 | 工期 | 说明 |
|------|-------------|------|------|
| 数组操作 | `expr/array_expr.hh/.cc` | 3 天 | `ARRAY[1,2,3]`, `arr[1]` |
| ROW 构造器 | `expr/row_constructor.hh/.cc` | 1 天 | `ROW(1,'a')` |
| EXECUTE 语句 | `grammar/grammar.hh/.cc` | 1 天 | `EXECUTE prep<N>`，scope 新增 `prepared_stmts` |
| VALUES 独立语句 | `grammar/grammar.cc` | 1 天 | `VALUES (1,'a')` 作为 FROM 源 |
| GENERATED ALWAYS | `grammar/grammar.cc` DDL | 1 天 | `col INT GENERATED ALWAYS AS (a+b) STORED` |
| 分区表 DDL | `grammar/grammar.cc` DDL | 3 天 | `PARTITION BY RANGE/LIST/HASH` |

---

## 4. Track A-2：MySQL 语法补齐

### 4.1 现状分析

dbfuzz 的 MySQL 驱动与 PostgreSQL 驱动相比存在以下差距：

| 维度 | PostgreSQL 驱动 | MySQL 驱动 | 差距 |
|------|----------------|-----------|------|
| 函数提取 | 从 `pg_proc` 动态提取 | **硬编码列表**（~80 个函数） | MySQL 未提取用户自定义函数 |
| 运算符提取 | 从 `pg_operator` 动态提取 | **硬编码列表**（~20 个运算符） | 固定不可扩展 |
| 类型提取 | 从 `pg_type` 动态提取 | **硬编码 5 种类型** | 类型覆盖有限 |
| 聚合函数 | 从 `pg_proc` 动态提取 | **硬编码列表**（~12 个） | 未提取 GROUP_CONCAT 等 |
| 窗口函数 | 从 `pg_proc` 动态提取 | **硬编码列表**（~7 个） | 缺少 NTILE/LAG/LEAD |
| 标识符引用 | `quote_ident()` | **裸标识符**（无反引号） | 保留字冲突 |
| Planner 配置 | 40+ settings | 17 optimizer_switch flags | 偏少但可接受 |
| JOIN 类型 | 5 种（含 full outer） | 4 种（缺 full outer） | MySQL 不支持 |
| 复合查询 | 6 种（含 intersect/except） | 2 种（仅 union） | MySQL 8.0.31+ 支持 INTERSECT/EXCEPT |

### 4.2 M0：反引号标识符引用（基础修复）

**问题：** MySQL 保留字（如 `order`, `group`, `key`, `table`）用作列名时必须用反引号包裹。dbfuzz 当前输出裸标识符，会导致语法错误。

**修改文件：**
- `src/schema/mysql.cc` — 新增 `quote_name()` 函数
- `src/schema/schema.hh` — 新增虚函数 `virtual string quote_name(const string &name)`
- `src/grammar/grammar.cc` — 所有标识符输出处调用 `schema->quote_name()`

**实现：**

```cpp
// schema.hh — schema 基类新增
virtual string quote_name(const string &name) { return name; }  // 默认不引号

// mysql.cc — schema_mysql 覆写
string quote_name(const string &name) override {
    return "`" + name + "`";  // MySQL 反引号
}

// postgres.cc — schema_postgres 覆写
string quote_name(const string &name) override {
    // PostgreSQL 使用双引号，但仅对保留字
    // 简化处理：所有标识符都用双引号包裹
    return "\"" + name + "\"";
}
```

**影响范围：** `grammar.cc` 中所有 `out << col.name`、`out << table.name`、`out << alias` 等输出点需要改为 `out << scope->schema->quote_name(name)`。

**工期：3 人天**

### 4.3 M1：MySQL 动态函数/运算符提取

**问题：** MySQL 驱动使用硬编码函数列表，而 PostgreSQL 驱动从 `pg_proc`/`pg_operator` 动态提取。

**方案：** 从 `information_schema.routines` 提取用户函数，并扩展硬编码列表。

**修改文件：** `src/schema/mysql.cc`

**新增提取：**

```cpp
// 从 information_schema.routines 提取存储函数
string q = "SELECT ROUTINE_NAME, ROUTINE_TYPE, DTD_IDENTIFIER "
           "FROM information_schema.routines "
           "WHERE ROUTINE_SCHEMA = '" + test_db + "' "
           "AND ROUTINE_TYPE = 'FUNCTION'";

// 扩展硬编码列表：新增 MySQL 特有函数
// IF(cond, then, else) — MySQL 的三元运算符
// IFNULL(expr1, expr2) — MySQL 的 NULL 处理
// NULLIF(expr1, expr2) — 标准 SQL
// GROUP_CONCAT(expr SEPARATOR ',') — MySQL 聚合
// JSON_ARRAY(), JSON_OBJECT() — MySQL JSON 函数
// JSON_EXTRACT(), JSON_UNQUOTE() — MySQL JSON 函数
// REGEXP_LIKE(), REGEXP_REPLACE() — MySQL 正则
// CONVERT(expr, type) — MySQL 类型转换
// CAST(expr AS type) — 标准 SQL
```

**工期：3 人天**

### 4.4 M2：ON DUPLICATE KEY UPDATE（MySQL UPSERT）

**问题：** dbfuzz 的 `upsert_stmt` 生成 PostgreSQL 语法 `ON CONFLICT ON CONSTRAINT ... DO UPDATE`，MySQL 需要 `ON DUPLICATE KEY UPDATE`。

**修改文件：**
- `src/grammar/grammar.hh` — `upsert_stmt` 的 `out()` 方法需要 DBMS 分支
- `src/grammar/grammar.cc` — 或 `upsert_stmt` 构造函数

**实现：**

```cpp
// grammar.hh — upsert_stmt out() 修改
virtual void out(std::ostream &out) {
    insert_stmt::out(out);
    if (scope->schema->target_dbms == "mysql" ||
        scope->schema->target_dbms == "tidb" ||
        scope->schema->target_dbms == "mariadb" ||
        scope->schema->target_dbms == "oceanbase") {
        // MySQL 语法
        out << " on duplicate key update ";
        out << *set_list;
    } else {
        // PostgreSQL 语法
        out << " on conflict on constraint " << constraint << " do update ";
        out << *set_list << " where " << *search;
    }
}
```

**注意：** MySQL 的 `ON DUPLICATE KEY UPDATE` 不需要引用 constraint 名称，也不支持 WHERE 子句。

**工期：1 人天**

### 4.5 M3：MySQL LIMIT 语法变体

**问题：** PostgreSQL 使用 `LIMIT count OFFSET offset`，MySQL 也支持但额外支持 `LIMIT offset, count` 语法。

**当前状态：** dbfuzz 的 `query_spec` 已生成 `LIMIT count` 格式，MySQL 兼容。无需修改。

但如果要增加语法多样性（提高覆盖率），可添加 `LIMIT offset, count` 变体。

**工期：0.5 人天**

### 4.6 M4：MySQL INTERSECT/EXCEPT

**问题：** MySQL 8.0.31+ 支持 `INTERSECT` 和 `EXCEPT`，但 dbfuzz 的 MySQL `compound_operators` 只有 `union distinct` 和 `union all`。

**修改文件：** `src/schema/mysql.cc`

```cpp
// mysql.cc — compound_operators 扩展
compound_operators.push_back("union distinct");
compound_operators.push_back("union all");
compound_operators.push_back("intersect");   // MySQL 8.0.31+
compound_operators.push_back("except");      // MySQL 8.0.31+
```

**注意：** 需检测 MySQL 版本，低于 8.0.31 不添加。

**工期：0.5 人天**

### 4.7 M5：MySQL GROUP_CONCAT 聚合

**问题：** `GROUP_CONCAT` 是 MySQL 特有的重要聚合函数，当前未注册。

**修改文件：** `src/schema/mysql.cc`

```cpp
// 在聚合函数注册区域添加
// GROUP_CONCAT(expr SEPARATOR ',') → text
// 注意：GROUP_CONCAT 有特殊的 SEPARATOR 语法
```

**特殊处理：** `GROUP_CONCAT` 的 SEPARATOR 子句需要 `win_funcall` 或 `funcall` 的特殊输出处理。

**工期：1 人天**

### 4.8 M6：MySQL IF() 和 IFNULL() 函数

**问题：** MySQL 的 `IF(cond, then, else)` 三元函数和 `IFNULL(expr, default)` 未注册。

**修改文件：** `src/schema/mysql.cc`

```cpp
// IF(bool, anyelement, anyelement) → anyelement
// IFNULL(anyelement, anyelement) → anyelement
```

**工期：0.5 人天**

### 4.9 M7：MySQL 窗口函数补全

**问题：** MySQL 驱动注释掉了 `NTILE`、`LAG`、`LEAD` 窗口函数。

**修改文件：** `src/schema/mysql.cc`

```cpp
// 取消注释并添加
// NTILE(int) → int
// LAG(anyelement, int) → anyelement
// LEAD(anyelement, int) → anyelement
// NTH_VALUE(anyelement, int) → anyelement
```

**工期：0.5 人天**

### 4.10 MySQL 补齐优先级

| 优先级 | 特性 | 工期 | Bug 检测价值 |
|--------|------|------|------------|
| **M0** | 反引号标识符引用 | 3 天 | **高** — 保留字冲突导致大量语法错误 |
| **M1** | 动态函数提取 + 扩展列表 | 3 天 | **高** — 更多函数覆盖 |
| **M2** | ON DUPLICATE KEY UPDATE | 1 天 | **高** — MySQL UPSERT 测试 |
| **M3** | LIMIT 语法变体 | 0.5 天 | 低 |
| **M4** | INTERSECT/EXCEPT | 0.5 天 | 中 |
| **M5** | GROUP_CONCAT | 1 天 | 中 |
| **M6** | IF/IFNULL | 0.5 天 | 中 |
| **M7** | 窗口函数补全 | 0.5 天 | 中 |

**MySQL 总计：10.5 人天**

---

## 5. 依赖关系与实施顺序

### 5.1 依赖图

```
                    ┌──────────────────────┐
                    │  Feature Flags 机制   │
                    │  + quote_name() 机制  │
                    └──────────┬───────────┘
                               │
              ┌────────────────┼────────────────┐
              │                │                │
              ▼                ▼                ▼
     ┌──────────────┐  ┌──────────────┐  ┌──────────────┐
     │ Track B      │  │ Track A-1    │  │ Track A-2    │
     │ Smoke 模式   │  │ PG 窗口帧    │  │ M0 反引号    │
     │ (严格复刻)   │  │ (P0)         │  │ (基础修复)   │
     └──────────────┘  └──────────────┘  └──────┬───────┘
                                                 │
              ┌──────────────┐                   ▼
              │ Track A-1    │           ┌──────────────┐
              │ PG 数据修改   │           │ Track A-2    │
              │ CTE (P0)     │           │ M1-M7 MySQL  │
              └──────┬───────┘           │ 语法补齐     │
                     │                   └──────────────┘
                     ▼
              ┌──────────────┐
              │ TxCheck CTE  │
              │ instrumentor │
              └──────────────┘

     ┌──────────────┐  ┌──────────────┐  ┌──────────────┐
     │ Track A-1    │  │ Track A-1    │  │ Track A-1    │
     │ 量化比较     │  │ GROUPING     │  │ JSON/JSONB   │
     │ (P1)         │  │ SETS (P1)    │  │ (P1)         │
     └──────────────┘  └──────────────┘  └──────────────┘

     ┌──────────────┐  ┌──────────────┐  ┌──────────────┐
     │ Track A-1    │  │ Track A-1    │  │ Track A-1    │
     │ 数组操作     │  │ EXECUTE      │  │ 分区表 DDL   │
     │ (P2)         │  │ (P3)         │  │ (P3)         │
     └──────────────┘  └──────────────┘  └──────────────┘
```

### 5.2 详细实施顺序

| Sprint | 任务 | 前置依赖 | 工期 | 可独立测试 |
|--------|------|---------|------|-----------|
| **S1** | Feature Flags + quote_name() 机制 | 无 | 2 天 | ✅ |
| **S1** | Smoke 模式核心（严格复刻 sqlsmith） | Feature Flags | 5 天 | ✅ |
| **S1** | PG 窗口帧子句 (P0) | Feature Flags | 3 天 | ✅ |
| **S2** | PG 数据修改 CTE (P0) | Feature Flags | 5 天 | ✅ |
| **S2** | TxCheck CTE instrumentor 适配 | 数据修改 CTE | 2 天 | ✅ |
| **S3** | PG 量化比较 (P1) | Feature Flags | 2 天 | ✅ |
| **S3** | PG GROUPING SETS/CUBE/ROLLUP (P1) | Feature Flags | 2 天 | ✅ |
| **S4** | M0 反引号标识符引用 | quote_name() | 3 天 | ✅ |
| **S4** | M1 MySQL 函数提取扩展 | 无 | 3 天 | ✅ |
| **S5** | M2 ON DUPLICATE KEY UPDATE | M0 | 1 天 | ✅ |
| **S5** | M3-M7 MySQL 语法补齐 | M0 | 3 天 | ✅ |
| **S5** | PG JSON/JSONB 阶段 1 | Feature Flags | 3 天 | ✅ |
| **S6** | PG JSON/JSONB 阶段 2 | JSON 阶段 1 | 3 天 | ✅ |
| **S6** | PG 数组操作 (P2) | Feature Flags | 3 天 | ✅ |
| **S7** | PG EXECUTE + ROW + VALUES (P3) | PREPARE 已有 | 3 天 | ✅ |
| **S7** | PG GENERATED ALWAYS + 分区表 DDL (P3) | 无 | 4 天 | ✅ |
| **S8** | 全面集成测试 + 文档 + Release Notes | 全部 | 5 天 | ✅ |

**总计：约 8 个 Sprint，52 人天**

---

## 6. 测试验证策略

### 6.1 Smoke 模式验证（对标 sqlsmith）

#### 6.1.1 行为对齐验证

在相同的 PostgreSQL 实例上，分别运行 sqlsmith 和 dbfuzz smoke 模式，对比：

```bash
# 1. 运行原生 sqlsmith（已构建）
/mnt/d/Jack.Xiao/dbtools/sqlsmith-master/build/sqlsmith \
    --target="host=localhost port=5432 dbname=testdb user=tpcc password=your_password" \
    --verbose --max-queries=5000 2> sqlsmith_output.log

# 2. 运行 dbfuzz smoke 模式
./build/dbfuzz --mode=smoke --verbose --max-queries=5000 \
    --postgres-db=testdb --postgres-port=5432 \
    --postgres-host=localhost --postgres-user=tpcc --postgres-pass=your_password \
    2> dbfuzz_smoke_output.log

# 3. 对比
# - 吞吐量（qps）应接近（±30%）
# - 错误率分布应类似（同类 production 的 ok/bad 比例）
# - 事务模式应一致（BEGIN/ROLLBACK 交替）
```

#### 6.1.2 阻抗反馈验证

```bash
# 对比两者的阻抗反馈 JSON 报告
# 检查黑名单 production 是否一致
# 检查各 production 的 ok/bad 比例趋势是否一致
```

#### 6.1.3 dry-run 语法验证

```bash
# 生成 SQL 但不执行，人工检查语法正确性
./build/dbfuzz --mode=smoke --dry-run --dump-all-queries \
    --postgres-db=testdb --postgres-port=5432 \
    --max-queries=100 > smoke_generated.sql

# 在 psql 中逐条验证
psql -d testdb -f smoke_generated.sql
```

### 6.2 PostgreSQL 语法验证

每个 PG 特性完成后：

```bash
# Smoke 模式验证新语法能被 PG 接受
./build/dbfuzz --mode=smoke --verbose \
    --postgres-db=testdb --postgres-port=5432 \
    --max-queries=500

# 验证点：
# 1. 新语法的 executed 率 > 20%
# 2. 无 crash (broken_count = 0)
# 3. verbose 输出中可看到新语法类型

# EET 模式验证（SELECT 相关特性）
./build/dbfuzz --mode=eet \
    --postgres-db=testdb --postgres-port=5432 \
    --db-test-num=3
```

### 6.3 MySQL 语法验证

每个 MySQL 特性完成后：

```bash
# Smoke 模式验证
./build/dbfuzz --mode=smoke --verbose \
    --mysql-db=testdb --mysql-port=3306 \
    --max-queries=500

# 验证点：
# 1. 反引号正确包裹标识符
# 2. ON DUPLICATE KEY UPDATE 语法正确
# 3. MySQL 函数被正确调用
# 4. 无 PG-only 语法泄漏（如 ON CONFLICT）
```

### 6.4 回归测试矩阵

| 模式 | PostgreSQL | MySQL | 验证内容 |
|------|-----------|-------|---------|
| Smoke | ✅ | ✅ | 新语法生成 + 无 crash |
| EET | ✅ | ✅ | QCN 测试含新语法 |
| TxCheck | ✅ | ✅ | 数据修改 CTE 依赖分析 |
| Cross | ✅+✅ | ✅+✅ | 跨库比较无异常 |

### 6.5 Feature Flags 正确性验证

```bash
# MySQL 模式下不应生成 PG-only 语法
./build/dbfuzz --mode=smoke --dry-run --dump-all-queries \
    --mysql-db=testdb --mysql-port=3306 \
    --max-queries=1000 > mysql_gen.sql

# 检查不包含 PG-only 语法
grep -i "ON CONFLICT" mysql_gen.sql     # 应为 0 匹配
grep -i "RETURNING" mysql_gen.sql       # 应为 0 匹配
grep -i "IS DISTINCT FROM" mysql_gen.sql # 应为 0 匹配
grep -i "MERGE INTO" mysql_gen.sql      # 应为 0 匹配（MySQL 不支持）

# 反引号存在
grep '`' mysql_gen.sql | head -5         # 应有匹配
```

---

## 7. 里程碑与时间线

### Sprint 1（Week 1-2）— 基础设施 + Smoke 模式 + 窗口帧

| 天 | 任务 | 交付物 |
|----|------|--------|
| D1-2 | Feature Flags + quote_name() | schema.hh 新增 flags + 虚函数 |
| D3-7 | Smoke 模式（严格复刻 sqlsmith） | smoke/ 模块完整实现 |
| D8-10 | PG 窗口帧子句 | window_frame 类 + 集成 |

**M1 验收：**
- ✅ Smoke 模式 `--verbose --max-queries=1000` 稳定运行
- ✅ 事务模式严格 `ROLLBACK; BEGIN; stmt; ROLLBACK;`
- ✅ 阻抗反馈 JSON 报告格式与 sqlsmith 一致
- ✅ 窗口帧 SQL 在 PostgreSQL 上执行成功

### Sprint 2（Week 3-4）— 数据修改 CTE

| 天 | 任务 | 交付物 |
|----|------|--------|
| D1-5 | PG 数据修改 CTE | cte_dml_item + data_modifying_cte |
| D6-7 | TxCheck instrumentor 适配 | CTE DML 正确分类 |
| D8-10 | 集成测试 + Bug 修复 | 回归通过 |

**M2 验收：**
- ✅ `WITH ... AS (DELETE ... RETURNING *) INSERT ...` 在 PG 上生成
- ✅ TxCheck 模式正确识别 CTE 内 DML
- ✅ Smoke + EET + TxCheck 三模式回归通过

### Sprint 3（Week 5）— PG P1 特性

| 天 | 任务 | 交付物 |
|----|------|--------|
| D1-2 | PG 量化比较 ALL/ANY/SOME | quantified_comparison 类 |
| D3-4 | PG GROUPING SETS/CUBE/ROLLUP | group_clause 扩展 |
| D5 | 集成测试 | 回归通过 |

**M3 验收：**
- ✅ `> ALL (SELECT ...)` 在 PG 上生成
- ✅ `GROUP BY GROUPING SETS (...)` 在 PG 上生成

### Sprint 4-5（Week 6-7）— MySQL 语法补齐

| 天 | 任务 | 交付物 |
|----|------|--------|
| D1-3 | M0 反引号标识符引用 | quote_name() 全量替换 |
| D4-6 | M1 MySQL 函数提取扩展 | 动态提取 + 扩展列表 |
| D7 | M2 ON DUPLICATE KEY UPDATE | upsert_stmt 分支 |
| D8-10 | M3-M7 其余 MySQL 补齐 | LIMIT/INTERSECT/GROUP_CONCAT/IF/窗口函数 |

**M4 验收：**
- ✅ MySQL 标识符使用反引号包裹
- ✅ `ON DUPLICATE KEY UPDATE` 语法正确生成
- ✅ MySQL 函数列表从 80 → 120+
- ✅ MySQL Smoke 模式 500 条无 crash

### Sprint 6（Week 8-9）— JSON + 数组

| 天 | 任务 | 交付物 |
|----|------|--------|
| D1-5 | PG JSON/JSONB 阶段 1+2 | json_extract_op + 路径运算符 |
| D6-8 | PG 数组操作 | array_constructor + array_subscript |
| D9-10 | 集成测试 | 回归通过 |

**M5 验收：**
- ✅ `col->>'key'` JSON 提取在 PG 上生成
- ✅ `ARRAY[1,2,3]` 数组构造在 PG 上生成

### Sprint 7（Week 10）— P3 特性

| 天 | 任务 | 交付物 |
|----|------|--------|
| D1-2 | PG EXECUTE + ROW 构造器 + VALUES | 3 个简单 production |
| D3-6 | PG GENERATED ALWAYS + 分区表 DDL | DDL 扩展 |
| D7-10 | 全面集成测试 | 4 模式 × 2 DBMS 回归 |

### Sprint 8（Week 11）— 收尾

| 天 | 任务 | 交付物 |
|----|------|--------|
| D1-3 | 全面回归测试 | 所有模式 × PG + MySQL |
| D4-5 | 文档更新 | user-guide + CLAUDE.md + release_notes |

**M6 最终验收：**
- ✅ Smoke 模式行为与 sqlsmith 原生工具完全对齐（逐项通过 2.2.6 检查表）
- ✅ 所有 PG P0-P3 特性在 PostgreSQL 上可生成
- ✅ 所有 MySQL M0-M7 特性在 MySQL 上可生成
- ✅ Feature Flags 正确隔离 DBMS 特性（MySQL 无 PG-only 语法泄漏）
- ✅ 4 种测试模式 × PostgreSQL + MySQL 回归测试通过
- ✅ Release Notes 更新

### 里程碑总览

| 里程碑 | Sprint | 核心交付 |
|--------|--------|---------|
| **M1** | S1 | Smoke 模式可用 + 窗口帧 |
| **M2** | S2 | 数据修改 CTE + TxCheck 适配 |
| **M3** | S3 | 量化比较 + GROUPING SETS |
| **M4** | S4-5 | MySQL 语法全面补齐 |
| **M5** | S6 | JSON/JSONB + 数组 |
| **M6** | S7-8 | P3 特性 + 全面回归 |
