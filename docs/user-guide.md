# dbfuzz 用户使用手册

> 版本：v1.0.11 | 最后更新：2026-06-09

## 目录

- [1. 项目概述](#1-项目概述)
- [2. 支持的数据库](#2-支持的数据库)
- [3. 构建与安装](#3-构建与安装)
- [4. 快速开始](#4-快速开始)
- [5. 测试模式详解](#5-测试模式详解)
  - [5.1 EET 模式 — 逻辑 Bug 检测](#51-eet-模式--逻辑-bug-检测)
  - [5.2 TxCheck 模式 — 事务 Bug 检测](#52-txcheck-模式--事务-bug-检测)
  - [5.3 Cross 模式 — 跨数据库比较测试](#53-cross-模式--跨数据库比较测试)
- [6. 命令行参考](#6-命令行参考)
- [7. Docker 测试](#7-docker-测试)
- [8. Bug 复现与最小化](#8-bug-复现与最小化)
- [9. 输出文件说明](#9-输出文件说明)
- [10. 配置与调优](#10-配置与调优)
- [11. 添加新的 DBMS 目标](#11-添加新的-dbms-目标)
- [12. 常见问题](#12-常见问题)
- [13. 架构概览](#13-架构概览)

---

## 1. 项目概述

dbfuzz 是一个统一的 C++17 数据库模糊测试工具，用于自动检测 DBMS 中的 Bug。它整合了三种互补的测试模式：

| 模式 | 目标 | 原理 |
|------|------|------|
| **EET** | 逻辑 Bug | 等价表达式变换 + 查询包含测试（QCN） |
| **TxCheck** | 事务 Bug | 依赖图分析 + 拓扑排序（G1a/G1b/G1c/G2/G-SI 异常） |
| **Cross** | 跨库差异 | 事务 + 等价变换三路比较 |

**工作流程**（以 EET 为例）：
1. 自动生成随机数据库 Schema（表、列、约束）
2. 填充随机测试数据
3. 生成随机 SQL 查询
4. 对查询应用等价变换（如 De Morgan 定律）
5. 执行原始查询和变换后的查询，比较结果
6. 结果不一致 → 报告 Bug

SQL 生成基于 [SQLsmith](https://github.com/anse1/sqlsmith) 引擎。

---

## 2. 支持的数据库

| DBMS | 协议 | 条件编译宏 | 备注 |
|------|------|-----------|------|
| MySQL 8.x | MySQL | `HAVE_MYSQL` | 支持非阻塞 API (`HAVE_MYSQL_NONBLOCK`) |
| MariaDB 10.x | MySQL | `HAVE_MARIADB` | 异步 API (`mysql_real_query_start`) |
| TiDB | MySQL | `HAVE_TIDB` | MySQL 兼容协议 |
| OceanBase | MySQL | `HAVE_MYSQL` | 需提供 host 参数 |
| PostgreSQL 16+ | libpq | `PQXX_FOUND` | 默认 DBMS |
| YugabyteDB | libpq | `PQXX_FOUND` | PostgreSQL 兼容 |
| CockroachDB | libpq | `PQXX_FOUND` | PostgreSQL 兼容 |
| GaussDB-M | libpq | `PQXX_FOUND` | GaussDB MySQL 兼容模式 |
| GaussDB-A | libpq | `PQXX_FOUND` | GaussDB Oracle 兼容模式 |
| SQLite3 | 本地文件 | `HAVE_LIBSQLITE3` | 无需网络 |
| ClickHouse | HTTP | 始终可用 | 不支持事务 |

---

## 3. 构建与安装

### 3.1 依赖安装

**Debian / Ubuntu：**
```bash
# 必需依赖
apt-get install -y g++ cmake libpqxx-dev

# 可选依赖（按需安装）
apt-get install -y libboost-regex-dev  # Boost 正则（备选 std::regex）
apt-get install -y libsqlite3-dev      # SQLite3 支持
apt-get install -y libmysqlclient-dev  # MySQL/MariaDB/TiDB/OceanBase 支持
```

**CentOS / RHEL：**
```bash
yum install -y gcc-c++ cmake libpqxx-devel boost-regex-devel
yum install -y sqlite-devel mysql-devel   # 可选
```

**macOS (Homebrew)：**
```bash
brew install cmake libpqxx boost
brew install sqlite mysql-client   # 可选
```

### 3.2 构建

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
```

构建完成后，可执行文件位于 `build/dbfuzz`。

### 3.3 验证构建

```bash
./build/dbfuzz --help
```

### 3.4 运行时依赖文件

dbfuzz 运行时需要以下错误模式文件（与可执行文件同目录）：

| 文件 | 用途 |
|------|------|
| `pgsqlerr.txt` | PostgreSQL / YugabyteDB / CockroachDB / GaussDB 预期错误列表 |
| `gaussdberr.txt` | GaussDB 特有错误模式列表 |
| `mysqlerr.txt` | MySQL 预期错误列表（内置于代码中） |

构建后需将源目录中的 `*err.txt` 文件复制到 `build/` 目录：
```bash
cp pgsqlerr.txt gaussdberr.txt build/
```

---

## 4. 快速开始

### 4.1 对 MySQL 做逻辑 Bug 检测

```bash
./build/dbfuzz --mode=eet \
    --mysql-db=testdb --mysql-port=3306 \
    --mysql-host=127.0.0.1 --mysql-user=root --mysql-pass=your_password \
    --db-test-num=5
```

### 4.2 对 PostgreSQL 做事务 Bug 检测

```bash
./build/dbfuzz --mode=txcheck \
    --postgres-db=testdb --postgres-port=5432 \
    --postgres-host=localhost --postgres-user=tpcc --postgres-pass=your_password \
    --output-or-affect-num=1
```

### 4.3 对 GaussDB-M 做跨模式比较测试

```bash
./build/dbfuzz --mode=cross \
    --gaussdb-m-db=testm --gaussdb-m-port=19995 \
    --gaussdb-m-host=your_host --gaussdb-m-user=your_user --gaussdb-m-pass=your_password
```

### 4.4 使用 Docker 一键测试

```bash
# 启动 MySQL 容器并运行 EET 测试
bash script/run_tests.sh eet mysql

# 启动 MariaDB 容器并运行 TxCheck 测试
bash script/run_tests.sh txcheck mariadb
```

---

## 5. 测试模式详解

### 5.1 EET 模式 — 逻辑 Bug 检测

**等价表达式变换测试 (Equivalent Expression Testing)**

EET 模式通过 Query Containment Testing (QCN) 检测 DBMS 的逻辑 Bug。原理：对 SQL 表达式应用数学等价变换后，结果应相同。

#### 测试流程

```
生成 Schema → 填充数据 → 生成 SQL → 应用等价变换 → 执行原始查询 → 执行变换查询 → 比较结果
                                                              ↓ 不一致
                                                         报告 Bug
```

#### QCN 测试器

| 测试器 | 变换目标 | 说明 |
|--------|---------|------|
| `qcn_select_tester` | SELECT 的 WHERE / SELECT list / HAVING / FROM 子查询 | 核心测试器，权重最高 |
| `qcn_cte_tester` | CTE (WITH clause) 子查询 | 测试公共表表达式 |
| `qcn_update_tester` | UPDATE 的 WHERE 子句 | 测试写操作条件 |
| `qcn_delete_tester` | DELETE 的 WHERE 子句 | 测试删除条件 |
| `qcn_insert_select_tester` | INSERT-SELECT 的 SELECT 部分 | 测试插入子查询 |

#### 等价变换规则

- **De Morgan 定律**：`NOT (A AND B)` ↔ `NOT A OR NOT B`
- **双重否定**：`NOT NOT A` ↔ `A`
- **常量传播**：`A AND TRUE` ↔ `A`
- **布尔恒等**：`A OR FALSE` ↔ `A`
- **比较等价**：`A = B` ↔ `B = A`、`A < B` ↔ `NOT (A >= B)`
- **NULL 处理**：`A IS NULL` ↔ `NOT (A IS NOT NULL)`
- **DISTINCT 等价**：`IS DISTINCT FROM` ↔ `IS NOT DISTINCT FROM` 取反

#### 测试器选择权重

每个测试轮随机选择测试器类型：
- 权重 1-3：SELECT 测试器
- 权重 4-6：CTE 测试器
- 权重 7-9：UPDATE 测试器
- 权重 10-12：DELETE 测试器

> **注意**：ClickHouse 不支持 UPDATE/DELETE 变换，仅使用 SELECT 和 CTE 测试器。

### 5.2 TxCheck 模式 — 事务 Bug 检测

**事务隔离级别异常检测**

TxCheck 模式通过构建事务依赖图并检测异常循环，发现 DBMS 事务实现中的 Bug。

#### 测试流程

```
生成 Schema → 填充数据 → 生成随机事务 → 并发执行事务 → 记录依赖关系
    → 构建依赖图 → 非事务顺序执行 → 拓扑排序 → 比较结果 → 检测异常
```

#### 检测的异常类型

| 异常 | 名称 | 描述 |
|------|------|------|
| **G1a** | Aborted Reads | 已提交事务读取了已回滚事务的写入 |
| **G1b** | Intermediate Reads | 事务读取了其他事务的中间（未最终提交）版本 |
| **G1c** | Circular Information Flow | 已提交事务之间存在 WW/WR 循环依赖 |
| **G2-item** | Item Anti-dependency Cycles | 存在 WW/WR/RW 循环依赖 |
| **G-SIa** | Snapshot Isolation Interference | 存在 WR/WW 依赖但缺少 start-depend |
| **G-SIb** | Snapshot Isolation Missed Effects | 存在仅含单条 RW 边的循环 |

#### 依赖类型

| 依赖 | 缩写 | 含义 |
|------|------|------|
| Write-Read | WR | 事务 B 读取事务 A 的写入 |
| Write-Write | WW | 事务 B 覆盖事务 A 的写入 |
| Read-Write | RW | 事务 B 在事务 A 写入后读取同一行 |
| Start-Depend | SD | 事务 B 在事务 A 提交后开始 |
| Version-Set-Depend | VS | 版本集依赖 |
| Overwrite-Depend | OW | 覆写依赖 |

#### 语句分类（Instrumentation）

TxCheck 对每条语句进行插桩分类：

| 分类 | 代码 | 说明 |
|------|------|------|
| `SELECT_READ` | 0 | 只读 SELECT |
| `UPDATE_WRITE` | 1 | UPDATE 语句 |
| `INSERT_WRITE` | 2 | INSERT 语句 |
| `DELETE_WRITE` | 3 | DELETE 语句 |
| `BEFORE_WRITE_READ` | 4 | 写前读（插桩） |
| `AFTER_WRITE_READ` | 5 | 写后读（插桩） |
| `VERSION_SET_READ` | 6 | 版本集读取 |

### 5.3 Cross 模式 — 跨数据库比较测试

**结合 TxCheck 和 EET 的综合测试**

Cross 模式在同一数据库上执行三重比较，覆盖事务和逻辑两个维度。

#### 四个 Oracle

| Oracle | 比较内容 | 检测目标 |
|--------|---------|---------|
| **TxCheck 输出 Oracle** | 事务执行结果 vs 非事务顺序执行结果 | 事务隔离异常 |
| **TxCheck 状态 Oracle** | 事务执行后数据库内容 vs 非事务执行后数据库内容 | 事务状态异常 |
| **EET 输出 Oracle** | 原始 SELECT 结果 vs 等价变换后 SELECT 结果 | 逻辑 Bug |
| **EET 状态 Oracle** | 变换执行后数据库内容 vs 原始执行后数据库内容 | 写操作逻辑异常 |

#### Cross 测试流程

```
1. 生成 Schema + 数据
2. 生成事务 + 非事务语句序列
3. 执行事务路径 → 记录结果 + 数据库状态
4. 提取拓扑排序路径（非事务等价序列）
5. 执行非事务路径 → 记录结果 + 数据库状态
6. 对 SELECT 语句应用等价变换 → 执行变换路径
7. 三路比较 → 报告差异
```

> **注意**：Cross 模式仅对 SELECT 类语句应用等价变换，不修改写操作（UPDATE/INSERT/DELETE）。

---

## 6. 命令行参考

### 6.1 必需选项

```
--mode=txcheck|eet|cross
```

### 6.2 数据库连接选项

每组选项至少提供一组：

#### MySQL
```
--mysql-db=str        数据库名（必需）
--mysql-port=int      端口号（默认：3306）
--mysql-host=str      主机地址（默认：127.0.0.1）
--mysql-user=str      用户名（默认：root）
--mysql-pass=str      密码
```

#### MariaDB
```
--mariadb-db=str      数据库名
--mariadb-port=int    端口号
```

#### PostgreSQL
```
--postgres-db=str     数据库名
--postgres-port=int   端口号
--postgres-path=str   安装路径（默认：/usr/local/pgsql）
--postgres-host=str   主机地址（默认：localhost）
--postgres-user=str   用户名
--postgres-pass=str   密码
```

#### TiDB
```
--tidb-db=str         数据库名
--tidb-port=int       端口号
```

#### OceanBase
```
--oceanbase-db=str    数据库名
--oceanbase-port=int  端口号
--oceanbase-host=str  主机地址
```

#### YugabyteDB
```
--yugabyte-db=str     数据库名
--yugabyte-port=int   端口号
--yugabyte-host=str   主机地址
```

#### CockroachDB
```
--cockroach-db=str    数据库名
--cockroach-port=int  端口号
--cockroach-host=str  主机地址
```

#### GaussDB-M（MySQL 兼容模式）
```
--gaussdb-m-db=str    数据库名
--gaussdb-m-port=int  端口号
--gaussdb-m-host=str  主机地址
--gaussdb-m-user=str  用户名
--gaussdb-m-pass=str  密码
```

#### GaussDB-A（Oracle 兼容模式）
```
--gaussdb-a-db=str    数据库名
--gaussdb-a-port=int  端口号
--gaussdb-a-host=str  主机地址
--gaussdb-a-user=str  用户名
--gaussdb-a-pass=str  密码
```

#### SQLite
```
--sqlite=file         数据库文件路径
```

#### ClickHouse
```
--clickhouse-db=str   数据库名
--clickhouse-port=int 端口号
```

### 6.3 通用选项

```
--seed=int            随机种子（默认：随机）
--cpu-affinity=int    绑定到指定 CPU 核心
--ignore-crash        忽略 crash 类 Bug，继续测试
--help                打印帮助信息
```

### 6.4 TxCheck 专用选项

```
--output-or-affect-num=int    生成语句的行数限制
--reproduce-sql=file          复现 Bug 的 SQL 文件
--reproduce-tid=file          复现 Bug 的事务 ID 文件
--reproduce-usage=file        复现 Bug 的语句分类文件
--reproduce-backup=file       复现 Bug 的数据库备份文件
--min                         最小化复现测试用例
```

### 6.5 EET 专用选项

```
--db-test-num=int     每个数据库的 QCN 测试轮数（默认：50）
--db-table-num=int    每个数据库生成的表数量
```

---

## 7. Docker 测试

### 7.1 docker-compose.yml 提供的容器

| 服务 | 镜像 | 端口 | 可用 Profile |
|------|------|------|-------------|
| mysql | `mysql:8.0` | 3306 | mysql, txcheck, cross, eet |
| mariadb | `mariadb:10.11` | 3307 | mariadb, txcheck |
| postgres | `postgres:16` | 5432 | postgres, eet |
| tidb | `pingcap/tidb:latest` | 4000 | tidb, txcheck, eet |
| clickhouse | `clickhouse-server:latest` | 8123, 9000 | clickhouse, eet |

### 7.2 手动启动容器

```bash
# 启动指定 DBMS
docker compose --profile mysql up -d

# 启动多个
docker compose --profile mysql --profile postgres up -d

# 查看运行状态
docker compose ps

# 停止并清理
docker compose --profile mysql down
```

### 7.3 使用测试脚本

```bash
# 一键构建 + 启动容器 + 测试
bash script/run_tests.sh eet mysql
bash script/run_tests.sh txcheck mariadb
bash script/run_tests.sh cross mysql
```

脚本自动执行：
1. 启动对应的 Docker 容器
2. 等待 DBMS 就绪（最长 60 秒）
3. 构建 dbfuzz（如未构建）
4. 运行 10 轮测试
5. 报告结果并检查 `found_bugs/` 目录
6. 停止容器

---

## 8. Bug 复现与最小化

### 8.1 TxCheck Bug 复现

当 TxCheck 检测到异常后，会在 `found_bugs/` 目录生成复现所需文件。使用以下命令复现：

```bash
./build/dbfuzz --mode=txcheck \
    --mysql-db=testdb --mysql-port=3306 \
    --reproduce-sql=final_stmts.sql \
    --reproduce-tid=final_tid.txt \
    --reproduce-usage=final_stmt_use.txt \
    --reproduce-backup=mysql_bk.sql
```

#### 复现所需文件

| 文件 | 说明 |
|------|------|
| `final_stmts.sql` | 触发 Bug 的 SQL 语句序列 |
| `final_tid.txt` | 每条语句所属的事务 ID |
| `final_stmt_use.txt` | 每条语句的分类代码（0-6） |
| `mysql_bk.sql` / `db_backup.sql` | 数据库备份（恢复到测试前状态） |

### 8.2 最小化测试用例

添加 `--min` 参数自动最小化复现用例：

```bash
./build/dbfuzz --mode=txcheck \
    --mysql-db=testdb --mysql-port=3306 \
    --reproduce-sql=final_stmts.sql \
    --reproduce-tid=final_tid.txt \
    --reproduce-usage=final_stmt_use.txt \
    --reproduce-backup=mysql_bk.sql \
    --min
```

最小化过程（两阶段）：
1. **事务级约减**：尝试逐个删除整个事务，若 Bug 仍存在则保留删除
2. **语句级约减**：尝试逐条删除语句，若 Bug 仍存在则保留删除

最小化结果保存到：
- `min_stmts.sql` — 最小化后的 SQL
- `min_tid.txt` — 最小化后的事务 ID
- `min_usage.txt` — 最小化后的语句分类

### 8.3 EET Bug 复现

EET 模式检测到的 Bug 保存为：

| 文件 | 说明 |
|------|------|
| `db_setup.sql` | 数据库 Schema + 初始数据 |
| `original_query.sql` | 原始查询 |
| `transformed_query.sql` | 等价变换后的查询 |
| `original_result.out` | 原始查询结果 |
| `transformed_result.out` | 变换查询结果 |

### 8.4 Cross 模式 Bug 复现

Cross 模式 Bug 报告包含完整上下文：

| 文件 | 说明 |
|------|------|
| `db_backup.sql` | 数据库备份 |
| `normal_stmts.sql` | 非事务语句序列 |
| `eet_select_stmts.sql` | EET 变换后的 SELECT 语句 |
| `tx_results.out` | 事务执行结果 |
| `normal_results.out` | 非事务执行结果 |
| `eet_select_results.out` | EET 变换执行结果 |

Cross 模式最小化结果保存在 `minimized/` 子目录。

---

## 9. 输出文件说明

### 9.1 运行输出

| 文件 / 目录 | 说明 |
|-------------|------|
| `db_setup.sql` | 当前数据库的 Schema 定义和初始数据 |
| `gen_stmts.sql` | 当前轮次生成的 SQL 语句 |
| `bug_trigger_stmt.sql` | 触发 Bug 的 SQL（EET 模式） |

### 9.2 Bug 报告目录 (`found_bugs/`)

每次发现 Bug，dbfuzz 在 `found_bugs/` 目录下创建带时间戳的子目录：

```
found_bugs/
├── 2026-06-09_14-30-22_eet_mysql/
│   ├── db_setup.sql
│   ├── original_query.sql
│   ├── transformed_query.sql
│   ├── original_result.out
│   └── transformed_result.out
├── 2026-06-09_14-35-10_txcheck_postgres/
│   ├── final_stmts.sql
│   ├── final_tid.txt
│   ├── final_stmt_use.txt
│   ├── db_backup.sql
│   └── dependency_graph.txt
└── ...
```

---

## 10. 配置与调优

### 10.1 随机种子

使用 `--seed` 实现可重复测试：

```bash
# 固定种子 → 相同 Schema + 相同语句
./build/dbfuzz --mode=eet --postgres-db=testdb --postgres-port=5432 --seed=42
```

### 10.2 CPU 亲和性

在多核机器上绑定到单个核心，减少调度抖动：

```bash
./build/dbfuzz --mode=txcheck --mysql-db=testdb --mysql-port=3306 --cpu-affinity=2
```

### 10.3 忽略 Crash 类 Bug

专注检测逻辑 Bug 时，忽略 crash 导致的终止：

```bash
./build/dbfuzz --mode=eet --postgres-db=testdb --postgres-port=5432 --ignore-crash
```

### 10.4 调整测试轮数

EET 模式：
```bash
# 每个数据库跑 100 轮 QCN 测试
./build/dbfuzz --mode=eet --postgres-db=testdb --postgres-port=5432 --db-test-num=100
```

TxCheck 模式：
```bash
# 限制每条语句影响的行数
./build/dbfuzz --mode=txcheck --mysql-db=testdb --mysql-port=3306 --output-or-affect-num=3
```

### 10.5 错误处理策略

dbfuzz 对异常采用分级处理：

| 异常类型 | 处理方式 |
|---------|---------|
| 预期错误（SQL 语法、约束违反等） | 跳过当前语句，继续测试 |
| 服务 crash / 连接断开 | `abort()` → 终止当前测试轮 → 报告 Bug |
| 随机 SQL 生成失败 | 跳过当前轮次 → 重试 |
| 数据库初始化失败 | 跳过当前轮次 → 重建 Schema → 重试 |

---

## 11. 添加新的 DBMS 目标

### 11.1 步骤

1. **创建驱动文件**
   - `src/schema/newdb.hh` — 头文件，定义 `schema_newdb` 和 `dut_newdb` 类
   - `src/schema/newdb.cc` — 实现文件

2. **实现 `dut_base` 接口**
   - `test()` — 执行 SQL 语句
   - `reset()` — 清空数据库
   - `backup()` — 备份当前状态
   - `reset_to_backup()` — 恢复到备份状态
   - `is_expected_error()` — 判断是否为预期错误

3. **实现 `schema` 子类**
   - 从系统目录提取类型、表、列、算子、函数信息

4. **注册到 CMake**
   - 在根 `CMakeLists.txt` 添加客户端库检测
   - 在 `src/CMakeLists.txt` 的 `schema_objs` 中添加源文件

5. **注册到运行时**
   - `src/core/general_process.hh` — 添加 `#include "schema/newdb.hh"`
   - `src/core/general_process.cc` — 在 `get_schema()`、`dut_setup()`、`save_backup_file()`、`fork_db_server()` 中添加分支
   - `src/core/dbms_info.cc` — 添加 CLI 选项解析
   - `src/main.cc` — 添加帮助文本和正则匹配

6. **添加预期错误文件**
   - 创建 `newdberr.txt`，列出该 DBMS 的常见预期错误消息

### 11.2 参考实现

| DBMS | 文件 | 适用场景 |
|------|------|---------|
| PostgreSQL | `postgres.hh/cc` | 基于 libpq 的 DBMS |
| MySQL | `mysql.hh/cc` | 基于 libmysqlclient 的 DBMS |
| GaussDB | `gaussdb.hh/cc` | PostgreSQL 协议兼容但有方言差异的 DBMS |
| ClickHouse | `clickhouse.hh/cc` | HTTP API 的 DBMS |

---

## 12. 常见问题

### Q: 构建时报 `libpqxx not found`
**A:** 安装 libpqxx 开发库：`apt-get install libpqxx-dev`。PostgreSQL 支持是必需的。

### Q: 运行时报 `pgsqlerr.txt: No such file`
**A:** 将 `pgsqlerr.txt`（和 `gaussdberr.txt`，如果使用 GaussDB）复制到 `build/` 目录。

### Q: 测试总是很快就结束
**A:** 检查 `--db-test-num`（EET）或 `--output-or-affect-num`（TxCheck）是否过小。默认值通常足够。

### Q: MySQL 非阻塞模式不生效
**A:** 需要 MySQL 8.x 客户端库。CMake 会自动检测 `mysql_real_query_nonblocking`，如果未检测到则回退到阻塞模式。检查构建日志中是否有 `HAVE_MYSQL_NONBLOCK` 的定义。

### Q: PostgreSQL backup/restore 失败
**A:** v1.0.11 已移除对 pg_dump/psql 的依赖，改为 SQL 方式备份恢复。如仍遇到问题，检查 `db_setup.sql` 文件是否正确生成。

### Q: GaussDB 连接后 Schema 提取失败
**A:** GaussDB 的 `information_schema` 与标准 PostgreSQL 有差异。v1.0.11 已添加 GaussDB 兼容性处理。确保使用 `--gaussdb-m-*` 或 `--gaussdb-a-*` 参数（而非 `--postgres-*`）。

### Q: 如何只测试逻辑 Bug 而不触发 crash？
**A:** 使用 `--ignore-crash` 参数。注意：这会忽略所有 crash 类 Bug，仅报告逻辑差异。

### Q: 测试过程中 fuzzer 自身崩溃（abort）
**A:** v1.0.11 修复了大部分因随机 SQL 错误导致的误 abort。如仍发生，检查 stderr 输出中的具体错误信息。

---

## 13. 架构概览

### 13.1 模块结构

```
dbfuzz
├── core        — 共享工具（随机数、阻抗反馈、日志、Schema 管理）
├── grammar     — SQL 语句 AST 生成（SELECT/INSERT/UPDATE/DELETE/DDL）
├── expr        — 表达式 AST（常量、列引用、函数调用、布尔表达式、窗口函数）
├── schema      — Schema 提取 + DBMS 驱动（dut_base 接口 + 各 DBMS 实现）
├── txcheck     — 事务测试（插桩、依赖分析、异常检测、最小化）
├── eet         — 逻辑 Bug 测试（QCN 测试器：select/update/delete/cte/insert-select）
└── cross       — 跨模式比较测试（事务 + EET 三路比较）
```

### 13.2 CMake 对象库

| 库名 | 目录 | 职责 |
|------|------|------|
| `core_objs` | `src/core/` | 共享工具 |
| `grammar_objs` | `src/grammar/` | SQL 生成 |
| `expr_objs` | `src/expr/` | 表达式 AST |
| `schema_objs` | `src/schema/` | Schema + 驱动 |
| `txcheck_objs` | `src/txcheck/` | 事务测试 |
| `eet_objs` | `src/eet/` | 逻辑 Bug 测试 |
| `cross_objs` | `src/cross/` | 跨模式测试 |

### 13.3 条件编译

| 宏 | 启用条件 | 影响范围 |
|----|---------|---------|
| `HAVE_MYSQL` | libmysqlclient 存在 | MySQL/TiDB/OceanBase 驱动 |
| `HAVE_MYSQL_NONBLOCK` | `mysql_real_query_nonblocking` 存在 | MySQL 非阻塞 API |
| `HAVE_MARIADB` | `mysql_real_query_start` 存在 | MariaDB 异步 API |
| `HAVE_TIDB` | libmysqlclient 存在 | TiDB 驱动 |
| `HAVE_LIBSQLITE3` | libsqlite3 存在 | SQLite 驱动 |
| `HAVE_BOOST_REGEX` | Boost.Regex 存在 | 使用 Boost 正则（否则 std::regex） |
| `PQXX_FOUND` | libpqxx + libpq 存在 | PostgreSQL/Yugabyte/Cockroach/GaussDB 驱动 |

### 13.4 测试循环架构

所有三种模式采用 **fork-based** 循环：

```
主进程
├── fork() → 子进程 1：测试轮次 1
├── fork() → 子进程 2：测试轮次 2
├── ...
└── fork() → 子进程 N：测试轮次 N
```

每个子进程独立执行完整的测试流程（Schema 生成 → SQL 生成 → 执行 → 比较）。如果子进程检测到 Bug，调用 `abort()` 终止自身，主进程记录 Bug 信息后继续下一轮。

### 13.5 门禁验证

分阶段验证构建和功能：

```bash
bash script/gate/run_gate.sh 0   # Phase 0: 项目骨架
bash script/gate/run_gate.sh 1   # Phase 1: core + grammar + expr + schema
bash script/gate/run_gate.sh 2   # Phase 2: TxCheck + EET 集成
bash script/gate/run_gate.sh 3   # Phase 3: Cross 跨库测试
```
