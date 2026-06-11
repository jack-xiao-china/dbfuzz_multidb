# sqlsmith / sqlsmith_mysql / dbfuzz 差异分析报告

> 分析版本：sqlsmith (master, PostgreSQL) vs sqlsmith_mysql (MySQL fork) vs dbfuzz v1.0.11
> 日期：2026-06-09

## 目录

- [1. 总体定位对比](#1-总体定位对比)
- [2. 功能差异](#2-功能差异)
- [3. 实现逻辑差异](#3-实现逻辑差异)
- [4. 算法差异](#4-算法差异)
- [5. 适用场景差异](#5-适用场景差异)
- [6. 建议](#6-建议)

---

## 1. 总体定位对比

| 维度 | sqlsmith | sqlsmith_mysql | dbfuzz |
|------|----------|---------------|--------|
| **核心定位** | 随机 SQL 生成器 + 崩溃检测 | sqlsmith 的 MySQL 移植版 | 多模式 DBMS Bug 检测框架 |
| **检测策略** | 错误驱动（crash / timeout） | 错误驱动（crash） | 等价性验证（QCN / 依赖图） |
| **SQL 生成来源** | 原创引擎 | 复用 sqlsmith 引擎（未修改） | 基于 sqlsmith 引擎扩展 |
| **目标 Bug 类型** | Crash、Assertion、Timeout | Crash | 逻辑 Bug、事务异常、跨库差异 |
| **DBMS 支持** | 3 个（PostgreSQL / SQLite / MonetDB） | MySQL + 原始 3 个 | 11 个 |
| **测试模式** | 单模式 | 单模式 | 三模式（EET / TxCheck / Cross） |
| **执行模型** | 单进程循环 | 单进程循环 | Fork-based 多轮测试 |
| **Bug 复现** | RNG 状态重放（无最小化） | 无 | 完整复现 + 两阶段最小化 |
| **成熟度** | 生产级（118+ 已知 Bug） | 原型级（proof-of-concept） | 生产级（集成测试验证） |

**一句话总结**：

- **sqlsmith**：针对 PostgreSQL 的高效"随机查询生成 + 崩溃检测"工具，是 DBMS fuzzing 领域的标杆项目
- **sqlsmith_mysql**：在 sqlsmith 之上添加了最小化的 MySQL 连接和 Schema 提取代码，但未对 SQL 生成引擎做任何 MySQL 适配，属于 proof-of-concept 级别的移植
- **dbfuzz**：在 sqlsmith 引擎基础上，构建了面向**语义正确性**和**事务隔离级别**的多维度 Bug 检测框架，全面覆盖 11 个 DBMS

---

## 2. 功能差异

### 2.1 Bug 检测 Oracle（核心差异）

| Oracle 类型 | sqlsmith | sqlsmith_mysql | dbfuzz |
|-------------|----------|---------------|--------|
| **错误驱动** | ✅ crash / timeout / unexpected error | ✅ crash（仅 error 2006） | ✅ 基础检测（所有模式共享） |
| **等价性验证（QCN）** | ❌ | ❌ | ✅ 5 种 QCN 测试器 |
| **事务依赖图** | ❌ | ❌ | ✅ 6 种异常检测 |
| **跨模式比较** | ❌ | ❌ | ✅ 4 个 Oracle 三路比较 |

**三个工具的 Oracle 对比**：

```
sqlsmith:      生成 SQL → 执行 → 分类(OK/Timeout/Broken/Error) → Broken=Crash Bug
sqlsmith_mysql: 生成 SQL → 执行 → 分类(Syntax/Broken/Error) → Broken=Crash Bug（无超时检测）
dbfuzz EET:    生成 SQL → 等价变换 → 执行两者 → 比较结果 → 不同=逻辑 Bug
dbfuzz TxCheck: 生成事务 → 并发执行 → 依赖图分析 → 检测异常循环
dbfuzz Cross:  三路比较（事务 vs 非事务 vs 变换）
```

### 2.2 支持的 SQL 语句类型

| SQL 特性 | sqlsmith (PG) | sqlsmith_mysql | dbfuzz | 说明 |
|----------|:---:|:---:|:---:|------|
| SELECT | ✅ | ✅ 生成但不适配 | ✅ | 核心语句 |
| INSERT | ✅ | ✅ | ✅ | sqlsmith 支持 DEFAULT VALUES |
| UPDATE | ✅ | ✅ | ✅ | |
| DELETE | ✅ | ✅ | ✅ | |
| CTE (WITH) | ✅ | ✅ MySQL 8.0+ 兼容 | ✅ | |
| JOIN | ✅ | ✅ | ✅ | INNER/LEFT/RIGHT |
| LATERAL 子查询 | ✅ | ✅ 生成（MySQL 8.0.14+ 兼容） | ❌ | dbfuzz 缺失 |
| MERGE | ✅ | ❌ MySQL 无此语法 | ❌ | SQL:2003 标准 |
| UPSERT (ON CONFLICT) | ✅ | ⚠️ 生成 PG 语法，MySQL 失败 | ❌ | MySQL 用 ON DUPLICATE KEY |
| TABLESAMPLE | ✅ | ❌ MySQL 不支持 | ❌ | PG BERNOULLI/SYSTEM |
| FOR UPDATE/SHARE | ✅ | ⚠️ 部分兼容 | ❌ | MySQL 仅支持 FOR UPDATE |
| PREPARE 语句 | ✅ | ✅ | ❌ | |
| RETURNING 子句 | ✅ | ❌ MySQL 不支持 | ❌ | PG 独有 |
| IS DISTINCT FROM | ✅ | ❌ MySQL 用 <=> | ❌ | PG 语法 |
| BEGIN/COMMIT/ABORT | ❌ | ❌ | ✅ TxCheck | 事务控制 |
| 版本键（wkey/vkey） | ❌ | ❌ | ✅ | 依赖分析插桩 |

**sqlsmith_mysql 关键问题**：直接复用了 sqlsmith 的 PostgreSQL 语法生成器，未做任何 MySQL 适配。导致大量生成的 SQL 在 MySQL 上产生语法错误（RETURNING、ON CONFLICT、TABLESAMPLE、IS DISTINCT FROM 等），依赖阻抗反馈逐步过滤这些不兼容的语句类型。

### 2.3 表达式生成能力

| 表达式类型 | sqlsmith | sqlsmith_mysql | dbfuzz | 说明 |
|-----------|:---:|:---:|:---:|------|
| 常量（const_expr） | ✅ NULL cast | ✅ 相同 | ✅ 真实随机值 | |
| 列引用 | ✅ | ✅ | ✅ | |
| 函数调用 | ✅ 从 pg_proc 提取 | ❌ 未提取 MySQL 函数 | ✅ 从系统目录提取 | **关键差异** |
| CASE 表达式 | ✅ | ✅ | ✅ | |
| COALESCE | ✅ | ✅ | ✅ | |
| 窗口函数 | ✅ | ✅ | ✅ | |
| 标量子查询 | ✅ | ✅ | ❌ | dbfuzz 缺失 |
| DISTINCT 谓词 | ✅ | ❌ MySQL 不兼容 | ✅ | |
| BETWEEN | ❌ | ❌ | ✅ | dbfuzz 额外 |
| LIKE | ❌ | ❌ | ✅ | dbfuzz 额外 |
| IN 子查询 | ❌ | ❌ | ✅ | dbfuzz 额外 |
| EXISTS | ✅ | ✅ | ✅ | |
| **等价变换** | ❌ | ❌ | ✅ | dbfuzz 核心 |

**sqlsmith_mysql 的关键缺陷**：
- 未从 MySQL 提取函数/算子/聚合，导致生成的表达式中函数调用和比较运算符极度受限
- 仅从 `information_schema.columns.data_type` 获取类型名，无 OID 映射、无伪类型处理

### 2.4 DBMS 支持

| DBMS | sqlsmith | sqlsmith_mysql | dbfuzz |
|------|:---:|:---:|:---:|
| PostgreSQL | ✅ 完整 | ✅ 完整 | ✅ |
| SQLite | ✅ | ✅ | ✅ |
| MonetDB | ✅ | ✅ | ❌ |
| **MySQL 8.x** | ❌ | ✅ 基本 | ✅ 完整 |
| MariaDB 10.x | ❌ | ⚠️ 可能兼容 | ✅ |
| TiDB | ❌ | ⚠️ 未测试 | ✅ |
| OceanBase | ❌ | ❌ | ✅ |
| YugabyteDB | ❌ | ❌ | ✅ |
| CockroachDB | ❌ | ❌ | ✅ |
| GaussDB-M | ❌ | ❌ | ✅ |
| GaussDB-A | ❌ | ❌ | ✅ |
| ClickHouse | ❌ | ❌ | ✅ |

### 2.5 MySQL Schema 提取对比

| 提取内容 | sqlsmith (PG) | sqlsmith_mysql | dbfuzz (MySQL) |
|---------|--------------|---------------|---------------|
| 表 | `information_schema.tables` | `information_schema.tables` | `information_schema.tables` |
| 列 | `pg_attribute`（OID 映射） | `information_schema.columns`（名称映射） | `information_schema.columns` + 类型 OID |
| 算子 | `pg_operator`（完整） | ❌ 未提取 | 从 `mysql.func` / 硬编码 |
| 函数 | `pg_proc`（完整） | ❌ 未提取 | 从系统目录 + 硬编码 |
| 聚合 | `pg_proc (prokind='a')` | ❌ 未提取 | 从系统目录 + 硬编码 |
| 约束 | `pg_constraint` (FK/U/PK) | ❌ 未提取 | FK/PK |
| 类型系统 | OID + 伪类型完整体系 | 仅 `data_type` 字符串 | 类型名 + 兼容性映射 |
| 标识符引用 | `quote_ident()` | 双引号 `"`（MySQL 需反引号 `` ` ``） | 反引号 `` ` `` |

### 2.6 基础设施

| 特性 | sqlsmith | sqlsmith_mysql | dbfuzz |
|------|:---:|:---:|:---:|
| 构建系统 | CMake + Autotools | Autotools | CMake |
| Docker 测试 | ❌ | ❌ | ✅ |
| 测试脚本 | ❌ | ❌ | ✅ |
| 门禁验证 | ❌ | ❌ | ✅ |
| AST 可视化 | ✅ GraphML | ✅ GraphML | ❌ |
| RNG 状态序列化 | ✅ | ✅ | ❌ |
| 数据库日志 | ✅ PG 集中日志 | ✅ PG 集中日志 | ❌ |
| 预期错误过滤 | boring_sqlstates + known + known_re | known + known_re | *err.txt + 内联正则 |
| CI/CD | ✅ GitHub Actions | ❌ | ❌ |
| 语句超时 | ✅ 1s | ❌ | ❌ |
| 事务保护 | ✅ BEGIN/ROLLBACK | ❌ 无事务包裹 | ✅ backup/restore |

---

## 3. 实现逻辑差异

### 3.1 执行模型

**sqlsmith / sqlsmith_mysql：单进程 + 无限循环**
```
main()
├── 创建 schema + scope
├── 创建 dut + loggers
└── while(1):                    // 外层：连接恢复
    └── while(1):                // 内层：主循环
        ├── statement_factory()  // 生成 AST
        ├── gen->out(s)          // AST → SQL
        ├── dut->test(s)         // PG: ROLLBACK;BEGIN;stmt;ROLLBACK
        │                        // MySQL: 直接执行（无事务保护）
        └── logger->executed/error  // 记录结果
```

**dbfuzz：Fork-based 多轮测试**
```
main()
├── 创建 dbms_info
└── mode_run()                   // eet_run / txcheck_run / cross_run
    └── while(1):                // 主循环
        ├── fork()               // 每轮 fork 子进程
        ├── [子进程]:
        │   ├── generate_database()   // 生成 Schema + 数据
        │   ├── dut->backup()         // 备份
        │   ├── 生成测试语句
        │   ├── 执行 + 比较
        │   └── exit(0) / abort()
        └── [父进程]:
            └── waitpid()             // 等待子进程
```

**关键差异**：

| 方面 | sqlsmith (PG) | sqlsmith_mysql | dbfuzz |
|------|--------------|---------------|--------|
| 数据库状态 | 在已有库上运行，ROLLBACK 恢复 | 直接执行，**不恢复状态** | 每轮生成新库，fork 隔离 |
| 状态一致性 | ✅ 每条语句独立 | ❌ 可能累积副作用 | ✅ 每轮全新 |
| 进程隔离 | ❌ 单进程 | ❌ 单进程 | ✅ fork |
| 吞吐量 | ~200-300 q/s | ~200-300 q/s（估计） | ~数 q/min |

### 3.2 连接管理

**sqlsmith (PG)**：
```cpp
// libpqxx 连接
c.set_variable("statement_timeout", "'1s'");   // 1 秒超时
c.set_variable("client_min_messages", "'ERROR'");
// 两个连接：一个 schema 提取，一个测试
```

**sqlsmith_mysql**：
```cpp
// libmysqlclient 连接，通过环境变量配置
#define MYSQL_HOST getenv("SQLSMITH_MYSQL_HOST")
#define MYSQL_PORT getenv("SQLSMITH_MYSQL_PORT")
// 无超时设置
// 无事务包裹
```

**dbfuzz (MySQL)**：
```cpp
// libmysqlclient 连接，通过 CLI 参数配置
--mysql-host --mysql-port --mysql-user --mysql-pass
// 支持非阻塞 API（HAVE_MYSQL_NONBLOCK）
// 阻塞检测 + 进程 ID 获取
```

### 3.3 错误处理策略

**sqlsmith (PG)**：
```
try:
  execute(query)  // 包裹在 BEGIN/ROLLBACK 中
catch dut::failure:
  if broken: re-throw → 外层 sleep(1s) + 重连
  if timeout: logger → 继续
  if syntax: logger → 继续
  else: logger → 继续
```

**sqlsmith_mysql**：
```
try:
  execute(query)  // 直接执行，无事务
catch mysql_error:
  if errno==1064 or 1149: syntax → 继续
  if errno==2006: broken → re-throw → 外层 sleep(1s) + 重连
  else: failure → 继续
```

**dbfuzz**（v1.0.11 改进后）：
```
try:
  execute(query)
catch exception:
  if is_expected_error(): skip → 继续
  if BUG or CONNECTION_FAIL: abort()  // 仅真正的 Bug 才终止
  else: skip → 继续（重试下一轮）
```

### 3.4 sqlsmith_mysql 的代码量评估

| 文件 | 代码行数 | 说明 |
|------|---------|------|
| `mysql.cc` | ~170 行 | MySQL 连接 + Schema + DUT |
| `mysql.hh` | ~30 行 | 头文件声明 |
| **新增总量** | **~200 行** | 其余文件全部复用 sqlsmith 原始代码 |

sqlsmith_mysql 的 MySQL 适配代码极为精简，仅实现了最基本的连接和 Schema 提取，未对 SQL 生成引擎做任何 MySQL 定制化。

---

## 4. 算法差异

### 4.1 阻抗反馈（Impedance Feedback）

三者共享同一套阻抗反馈代码（`impedance.cc`），核心逻辑相同：

```cpp
// 跟踪每种 AST 节点类型的成功/失败次数
occurances_in_ok_query[typeid(*p).name()]++;
occurances_in_failed_query[typeid(*p).name()]++;

// 黑名单判定：失败率 > 99% 且样本 >= 100 → 黑名单
bool matched(name) {
  if (failed_count < 100) return true;
  if (error_rate > 0.99) return false;
  return true;
}
```

**差异**：

| 特性 | sqlsmith (PG) | sqlsmith_mysql | dbfuzz |
|------|--------------|---------------|--------|
| 基础 matched() | ✅ | ✅ | ✅ |
| retry/limit/fail 统计 | ✅ | ✅ | ❌ 简化 |
| JSON 报告输出 | ✅ | ✅ | ❌ |
| 黑名单学习效率 | 高（SQL 语法正确率高） | **低**（大量 PG 语法导致 MySQL 报错，需更长时间收敛） | 中 |

**sqlsmith_mysql 的阻抗反馈困境**：由于 SQL 生成器未适配 MySQL，大量生成的 SQL 本身就有语法错误（RETURNING、ON CONFLICT、TABLESAMPLE 等），导致阻抗反馈在运行初期大量黑名单，有效查询比例极低。实测在 PostgreSQL 上 500 条查询错误率约 63%（多为类型不匹配），在 MySQL 上预计更高。

### 4.2 QCN 等价变换算法（dbfuzz 独有）

```
原始布尔表达式 E
    ↓ equivalent_transform(E)
变换后表达式 E'

数学保证：E ≡ E' （语义等价）

变换规则：
1. De Morgan: NOT(A AND B) → NOT A OR NOT B
2. 双重否定: NOT(NOT A) → A
3. 常量传播: A AND TRUE → A, A OR FALSE → A
4. 比较对称: A = B → B = A
5. NULL 等价: A IS NULL → NOT(A IS NOT NULL)
6. DISTINCT: A IS DISTINCT FROM B → NOT(A IS NOT DISTINCT FROM B)
```

**sqlsmith 和 sqlsmith_mysql 均缺失此算法**——它们不验证查询结果的正确性，只检测 crash。

### 4.3 依赖图分析算法（dbfuzz TxCheck 独有）

```
输入：N 个并发事务的执行记录
输出：是否存在异常循环

1. 对每条语句插桩分类（SELECT_READ / UPDATE_WRITE / ...）
2. 构建依赖图（WR/WW/RW 边）
3. 检测循环：G1a / G1b / G1c / G2-item / G-SIa / G-SIb
```

**sqlsmith 和 sqlsmith_mysql 均完全缺失此算法**。

### 4.4 最小化算法

| 算法 | sqlsmith | sqlsmith_mysql | dbfuzz |
|------|:---:|:---:|:---:|
| 语句级约减 | ❌ | ❌ | ✅ |
| 事务级约减 | ❌ | ❌ | ✅ |
| 表达式约减 | ❌ | ❌ | ✅ |
| 数据库约减 | ❌ | ❌ | ✅ |
| RNG 状态重放 | ✅ | ✅ | ❌ |

### 4.5 查询执行效率

| 指标 | sqlsmith (PG) | sqlsmith_mysql | dbfuzz |
|------|:---:|:---:|:---:|
| 单条查询开销 | 极低（ROLLBACK+BEGIN+EXEC+ROLLBACK） | 低（直接执行） | 高（fork + 完整流程） |
| 吞吐量 | ~200-300 q/s | ~200-300 q/s（估计） | ~数 q/min |
| 超时机制 | ✅ statement_timeout=1s | ❌ 无 | ❌ 无 |
| 事务保护 | ✅ BEGIN/ROLLBACK | ❌ 无 | ✅ backup/restore |
| 连接复用 | 单连接复用 | 单连接复用 | 每轮新连接 |
| 有效查询比例 | 高（~36% 在 PG 上成功） | **低**（大量 PG 语法不兼容 MySQL） | 高（Schema 适配目标 DBMS） |

### 4.6 Schema 提取深度

| 提取内容 | sqlsmith (PG) | sqlsmith_mysql | dbfuzz |
|---------|:---:|:---:|:---:|
| 表/列 | ✅ 完整 | ✅ 基本 | ✅ 完整 |
| 算子目录 | ✅ pg_operator | ❌ | ✅ |
| 函数目录 | ✅ pg_proc | ❌ | ✅ |
| 聚合目录 | ✅ pg_proc (prokind='a') | ❌ | ✅ |
| 类型体系 | ✅ OID + 伪类型 + consistent() | ⚠️ 仅名称字符串 | ✅ 名称 + 兼容映射 |
| 约束 | ✅ FK/UNIQUE/PK | ❌ | ✅ FK/PK |
| 索引 | ✅ generate_indexes() | ✅ generate_indexes() | ✅ |
| Schema 过滤 | ✅ --exclude-catalog | ❌ | ✅ 硬编码 |

---

## 5. 适用场景差异

### 5.1 各工具最佳场景

**sqlsmith（PostgreSQL 原版）**

| 场景 | 原因 |
|------|------|
| PostgreSQL Crash Bug 检测 | 原生支持最完善，类型系统全覆盖 |
| 高吞吐 Fuzzing | 200-300 q/s，适合长时间跑 |
| 回归测试 | RNG 状态可序列化，精确重放 |
| 并行多实例测试 | 集中日志到 PG 数据库 |
| 新 PG 版本验证 | 完整伪类型 + 版本适配 |

**sqlsmith_mysql**

| 场景 | 原因 |
|------|------|
| MySQL 快速冒烟 | 快速验证 MySQL 是否会 crash |
| 学术研究 / 原型验证 | 作为 sqlsmith 移植到其他 DBMS 的参考实现 |

**不适用场景**：
- 不适合作为生产级 MySQL fuzzer（有效查询比例低）
- 不适合发现逻辑 Bug（仅有 crash oracle）
- 不适合检测 MySQL 特有 Bug（未提取 MySQL 函数/算子）

**dbfuzz**

| 场景 | 原因 |
|------|------|
| 逻辑 Bug 检测 | QCN 等价变换发现"返回错误结果但不 crash"的 Bug |
| 事务隔离级别验证 | TxCheck 检测 6 种事务异常 |
| 多 DBMS 对比测试 | 11 个 DBMS 驱动 + Cross 模式 |
| Bug 复现与定位 | 完整复现链 + 两阶段最小化 |
| MySQL/MariaDB 生态测试 | 完整 MySQL 驱动（算子/函数/类型适配） |
| 国产数据库测试 | GaussDB / OceanBase / TiDB |
| 发布前集成验证 | Docker + 门禁脚本 |

### 5.2 三者关系图

```
                           ┌───────────────────────────────────────┐
                           │           Bug 检测频谱                │
                           └───────────────────────────────────────┘

  Crash/Timeout                              逻辑/语义 Bug
  ◄────────────────────────────────────────────────────────────►

  sqlsmith (PG)      sqlsmith_mysql         dbfuzz EET
  (高吞吐 PG 测试)   (低效 MySQL 测试)      (等价变换 + 结果比较)

                                            dbfuzz TxCheck
                                            (事务依赖图分析)

                                            dbfuzz Cross
                                            (三路比较 + 最小化)

  效率: 高──────────────────────────────────────────────►低
  深度: 浅──────────────────────────────────────────────►深
```

### 5.3 功能完整性对比矩阵

| 功能项 | sqlsmith (PG) | sqlsmith_mysql | dbfuzz |
|--------|:---:|:---:|:---:|
| **Crash 检测** | ✅ | ✅ | ✅ |
| **Timeout 检测** | ✅ | ❌ | ❌ |
| **逻辑 Bug 检测** | ❌ | ❌ | ✅ |
| **事务 Bug 检测** | ❌ | ❌ | ✅ |
| **跨库对比** | ❌ | ❌ | ✅ |
| **MySQL 算子/函数提取** | N/A | ❌ | ✅ |
| **MySQL 语法适配** | N/A | ❌ | ✅ |
| **标识符正确引用** | ✅ | ❌（双引号 vs 反引号） | ✅ |
| **事务状态保护** | ✅ | ❌ | ✅ |
| **语句超时保护** | ✅ | ❌ | ❌ |
| **Bug 最小化** | ❌ | ❌ | ✅ |
| **Docker 集成** | ❌ | ❌ | ✅ |
| **多 DBMS 支持** | 3 个 | 4 个（含 MySQL） | 11 个 |

---

## 6. 建议

### 6.1 dbfuzz 从 sqlsmith 引入的高级 SQL 特性（优先级：高）

dbfuzz 的 SQL 生成引擎缺少以下 sqlsmith 已有的高级特性：

| 特性 | 优先级 | 价值 | 实现复杂度 | 参考来源 |
|------|--------|------|-----------|---------|
| **LATERAL 子查询** | 高 | 复杂关联查询，PG/MySQL 均支持 | 中 | sqlsmith grammar.cc |
| **MERGE 语句** | 高 | SQL:2003 标准 | 高 | sqlsmith grammar.cc |
| **FOR UPDATE/SHARE** | 高 | 锁模式 + 事务隔离交叉测试 | 低 | sqlsmith grammar.cc |
| **标量子查询 (atomic_subselect)** | 高 | WHERE 中嵌套 SELECT | 中 | sqlsmith expr.cc |
| **UPSERT (ON CONFLICT)** | 中 | PG/TiDB 冲突处理 | 中 | sqlsmith grammar.cc |
| **TABLESAMPLE** | 中 | 采样查询 | 低 | sqlsmith grammar.cc |
| **RETURNING 子句** | 中 | DML 返回值 | 低 | sqlsmith grammar.cc |
| **PREPARE/EXECUTE** | 低 | 预编译语句 | 低 | sqlsmith grammar.cc |

**建议实现顺序**：FOR UPDATE → RETURNING → atomic_subselect → LATERAL → MERGE → TABLESAMPLE → UPSERT

### 6.2 dbfuzz 从 sqlsmith 引入的基础设施（优先级：中）

| 特性 | 说明 | 价值 |
|------|------|------|
| **语句超时控制** | sqlsmith 使用 `statement_timeout=1s`；dbfuzz 无内置超时 | 避免测试卡死 |
| **RNG 状态序列化** | sqlsmith 可保存/恢复 RNG 状态用于精确重放 | 增强 Bug 复现能力 |
| **数据库集中日志** | sqlsmith 支持将错误日志写入 PG 数据库 | 大规模测试聚合分析 |
| **AST 可视化** | sqlsmith 可导出 GraphML 格式 AST | 调试 SQL 生成 |
| **CI/CD 集成** | sqlsmith 有 GitHub Actions workflow | 自动化回归 |

### 6.3 增强阻抗反馈（优先级：中）

当前 dbfuzz 仅保留了基础 `matched()` 判定，可引入 sqlsmith 的完整统计：

```
现有：matched(typeid) → bool
增强：
  - retry(name)   — 记录重试次数
  - limit(name)   — 记录达到重试上限的次数
  - fail(name)    — 记录显式失败次数
  - report(JSON)  — 输出 JSON 格式统计
```

### 6.4 增加 smoke 模式（优先级：中）

建议在 dbfuzz 中增加第四种模式 `--mode=smoke`：

```bash
./build/dbfuzz --mode=smoke --mysql-db=testdb --mysql-port=3306
```

**用途**：复用 sqlsmith 式的高吞吐随机查询做冒烟测试：
- 快速发现 Crash / Timeout
- 验证连接和基本功能
- 预热阻抗反馈
- 估计查询吞吐量

### 6.5 查询超时保护（优先级：高）

当前 dbfuzz 和 sqlsmith_mysql 均缺少查询超时机制。

**建议参考 sqlsmith 实现**：
- PostgreSQL: `SET statement_timeout = '5s'`
- MySQL: `SET SESSION max_execution_time = 5000`（毫秒）
- 通用: 子进程级别的 `alarm()` 或 `setitimer()`

### 6.6 对 sqlsmith_mysql 的评估与建议

sqlsmith_mysql 作为 sqlsmith 到 MySQL 的移植尝试，存在以下根本性问题：

| 问题 | 严重程度 | 说明 |
|------|---------|------|
| **未提取 MySQL 函数/算子** | 严重 | 生成的表达式极度受限，无法覆盖 MySQL 丰富的内置函数 |
| **未适配 MySQL 语法** | 严重 | RETURNING、ON CONFLICT、TABLESAMPLE 等 PG 语法导致大量语法错误 |
| **标识符引用错误** | 中 | 使用双引号而非反引号 |
| **无事务保护** | 中 | 写操作（INSERT/UPDATE/DELETE）会修改数据库状态且无法回滚 |
| **无超时保护** | 中 | 复杂查询可能永久阻塞 |
| **连接参数硬编码为环境变量** | 低 | 不如 CLI 参数灵活 |

**结论**：sqlsmith_mysql 是一个**最小化的概念验证**，不适合作为生产级 MySQL fuzzer 使用。dbfuzz 的 MySQL 驱动在以下方面全面优于 sqlsmith_mysql：

- ✅ 完整的 MySQL 算子/函数/类型提取
- ✅ 正确的 MySQL 语法生成
- ✅ 正确的标识符引用（反引号）
- ✅ 事务保护（backup/restore）
- ✅ 非阻塞 API 支持
- ✅ 阻塞检测
- ✅ 完整的错误模式匹配

**如需改进 sqlsmith_mysql**，需要：
1. 实现 MySQL 函数/算子/聚合提取（从 `mysql.proc` / `mysql.func` / `information_schema.routines`）
2. 在 grammar.cc 中为 MySQL 添加语法分支（去掉 RETURNING/TABLESAMPLE，改 ON CONFLICT 为 ON DUPLICATE KEY UPDATE）
3. 修复标识符引用为反引号
4. 添加 BEGIN/ROLLBACK 事务包裹
5. 添加 `SET max_execution_time` 超时

**但以上工作量本质上等同于在 dbfuzz 中已实现的功能，因此建议直接使用 dbfuzz 进行 MySQL 测试。**

### 6.7 总结：优先级排序

| 优先级 | 建议 | 预期收益 |
|--------|------|---------|
| **P0** | 查询超时保护 | 避免测试卡死（dbfuzz + sqlsmith_mysql 均缺失） |
| **P0** | FOR UPDATE / 锁模式 | 发现并发控制 Bug |
| **P1** | LATERAL 子查询 | 增加查询复杂度覆盖面 |
| **P1** | 标量子查询 (atomic_subselect) | WHERE 中嵌套 SELECT |
| **P1** | MERGE 语句 | SQL:2003 标准覆盖 |
| **P1** | smoke 模式（sqlsmith 式快速测试） | 快速冒烟 + 预热阻抗 |
| **P2** | RETURNING 子句 | DML 返回值测试 |
| **P2** | TABLESAMPLE | 采样查询测试 |
| **P2** | RNG 状态序列化 | 增强复现能力 |
| **P2** | 增强阻抗反馈统计 | 更好的自适应能力 |
| **P3** | UPSERT (ON CONFLICT) | 冲突路径测试 |
| **P3** | dry-run / dump 模式 | 开发调试 |
| **P3** | CI/CD GitHub Actions | 自动化回归 |
| **P3** | 数据库集中日志 | 大规模测试聚合 |
