# dbfuzz 用户使用手册

> 版本：v1.0.18 | 最后更新：2026-06-11

## 目录

- [1. 项目概述](#1-项目概述)
- [2. 支持的数据库](#2-支持的数据库)
- [3. 构建与安装](#3-构建与安装)
- [4. 快速开始](#4-快速开始)
- [5. 测试模式详解](#5-测试模式详解)
  - [5.1 EET 模式 — 逻辑 Bug 检测](#51-eet-模式--逻辑-bug-检测)
  - [5.2 TxCheck 模式 — 事务 Bug 检测](#52-txcheck-模式--事务-bug-检测)
  - [5.3 Cross 模式 — 跨数据库比较测试](#53-cross-模式--跨数据库比较测试)
  - [5.4 Smoke 模式 — 高频崩溃检测](#54-smoke-模式--高频崩溃检测)
- [6. 命令行参考](#6-命令行参考)
- [7. Docker 测试](#7-docker-测试)
- [8. Bug 复现与最小化](#8-bug-复现与最小化)
- [9. 输出文件说明](#9-输出文件说明)
- [10. 配置与调优](#10-配置与调优)
- [11. 添加新的 DBMS 目标](#11-添加新的-dbms-目标)
- [12. 常见问题](#12-常见问题)
- [13. 架构概览](#13-架构概览)
- [14. 集成测试结果](#14-集成测试结果)

---

## 1. 项目概述

dbfuzz 是一个统一的 C++17 数据库模糊测试工具，用于自动检测 DBMS 中的 Bug。它整合了四种互补的测试模式：

| 模式 | 目标 | 原理 |
|------|------|------|
| **EET** | 逻辑 Bug | 等价表达式变换 + 查询包含测试（QCN） |
| **TxCheck** | 事务 Bug | 依赖图分析 + 拓扑排序（G1a/G1b/G1c/G2/G-SI 异常） |
| **Cross** | 跨库差异 | 事务 + 等价变换三路比较 |
| **Smoke** | 崩溃检测 | 高频随机 SQL 生成 + 阻抗反馈（严格复刻 sqlsmith） |

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

**方式 1：使用系统包（默认）**
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
```

构建完成后，可执行文件位于 `build/dbfuzz`。

**方式 2：依赖打包（无需手动安装 DB 客户端库）**

适用于 CentOS/RHEL 等缺少 libpqxx-dev、libmysqlclient-dev 的环境：
```bash
# 一键构建（自动安装构建工具 + 下载编译所有依赖）
bash script/build_bundled.sh

# 或手动
cmake -B build -DUSE_BUNDLED_DEPS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

打包模式会从源码下载编译：SQLite3 amalgamation、MariaDB Connector/C、PostgreSQL libpq、libpqxx。首次构建约 5-10 分钟，产出自包含二进制（不依赖外部 .so 文件）。

> **前置要求**：g++、cmake、git、autoconf、openssl-devel（通过 `script/build_bundled.sh` 自动安装）。

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

### 4.5 对 PostgreSQL 做高频崩溃检测

```bash
# Smoke 模式（复刻 sqlsmith 行为，ROLLBACK 事务包裹）
./build/dbfuzz --mode=smoke \
    --postgres-db=testdb --postgres-port=5432 \
    --postgres-host=localhost --postgres-user=tpcc --postgres-pass=your_password \
    --verbose --max-queries=100000
```

### 4.6 保存/恢复 RNG 状态（可重复测试）

```bash
# 第一轮：运行并保存 RNG 状态
./build/dbfuzz --mode=smoke \
    --postgres-db=testdb --postgres-port=5432 \
    --verbose --rng-state-out=rng_state.dat

# 第二轮：恢复 RNG 状态精确复现
./build/dbfuzz --mode=smoke \
    --postgres-db=testdb --postgres-port=5432 \
    --verbose --rng-state=rng_state.dat
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

### 5.4 Smoke 模式 — 高频崩溃检测

**严格复刻 sqlsmith 原生运行时行为**

Smoke 模式是 dbfuzz 中最高效的崩溃检测模式，专注于发现 DBMS 中的 crash、assertion failure、segfault 等严重问题。它严格复刻了 sqlsmith 的运行时行为，适合大规模持续 fuzzing。

#### 核心特点

| 特性 | 说明 |
|------|------|
| **事务模式** | `ROLLBACK; BEGIN; <stmt>; ROLLBACK;` — 每条语句独立事务，自动回滚 |
| **超时控制** | `statement_timeout=1s` — 防止单条语句阻塞 |
| **阻抗反馈** | 基于执行成功率动态调整 SQL 生成权重，避免无效语法 |
| **连接恢复** | 双层连接恢复循环，自动重连断开的连接 |
| **已知错误过滤** | `known.txt` / `known_re.txt` 精确子串 + 正则匹配，减少噪声输出 |

#### 测试流程

```
连接 DBMS → 提取 Schema → 循环生成随机 SQL
    → ROLLBACK; BEGIN; <stmt>; ROLLBACK;
    → 记录成功/失败 → 更新阻抗反馈 → 检测 crash
```

#### 运行示例

```bash
# 基本使用
./build/dbfuzz --mode=smoke --postgres-db=testdb --postgres-port=5432 --verbose

# 限制查询数
./build/dbfuzz --mode=smoke --postgres-db=testdb --postgres-port=5432 --max-queries=10000

# 打印每条 SQL（调试用）
./build/dbfuzz --mode=smoke --postgres-db=testdb --postgres-port=5432 --dump-all-queries

# 仅生成不执行
./build/dbfuzz --mode=smoke --postgres-db=testdb --postgres-port=5432 --dry-run

# 保存/恢复 RNG 状态（可重复测试）
./build/dbfuzz --mode=smoke --postgres-db=testdb --postgres-port=5432 --rng-state-out=state.dat
./build/dbfuzz --mode=smoke --postgres-db=testdb --postgres-port=5432 --rng-state=state.dat

# Dump AST 为 GraphML
./build/dbfuzz --mode=smoke --postgres-db=testdb --postgres-port=5432 --dump-all-graphs

# 排除系统表
./build/dbfuzz --mode=smoke --postgres-db=testdb --postgres-port=5432 --exclude-catalog
```

#### 已知错误过滤

Smoke 模式支持通过配置文件过滤已知的预期错误，减少 stderr 噪声：

| 文件 | 格式 | 说明 |
|------|------|------|
| `known.txt` | 每行一个精确子串 | 匹配 stderr 输出中的精确错误消息 |
| `known_re.txt` | 每行一个正则表达式 | 匹配 stderr 输出中的正则模式 |

过滤后的错误仅传递给阻抗反馈（语法适应），不输出到 cerr_logger。

#### 与其他模式的区别

| 维度 | Smoke | EET/TxCheck/Cross |
|------|-------|-------------------|
| 目标 | 崩溃检测 | 逻辑/事务 Bug 检测 |
| 事务处理 | 每条语句独立回滚 | 完整事务序列 |
| 结果比较 | 无 | 原始 vs 变换/事务 vs 非事务 |
| 性能 | 高（~10K queries/min） | 低（完整测试流程） |
| Bug 类型 | crash/assertion failure | 逻辑错误/事务异常 |

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

### 6.6 Smoke 专用选项

```
--max-queries=int     最大查询数（默认：无限）
--verbose             打印进度信息到 stderr
--dump-all-queries    打印每条生成的 SQL 语句
--dump-all-graphs     将 AST dump 为 GraphML XML 文件
--rng-state-out=file  保存 RNG 状态到文件（用于断点续测）
--dry-run             仅生成 SQL，不执行
--exclude-catalog     跳过 pg_catalog/information_schema 系统表
```

### 6.7 通用选项补充

```
--rng-state=str       从之前的运行恢复 RNG 状态（用于精确复现）
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

### Q: Smoke 模式如何保存和恢复测试进度？
**A:** 使用 `--rng-state-out=state.dat` 在退出时保存 RNG 状态，下次运行时用 `--rng-state=state.dat` 恢复。注意：RNG 状态恢复的是随机数序列，不是数据库状态，因此 Schema 会重新生成但 SQL 序列相同。

### Q: Cross 模式 minimize 时出现段错误 (SIGSEGV)？
**A:** v1.0.17 已添加 SIGSEGV 信号处理器，段错误时子进程优雅退出（exit code 2），父进程继续测试。Bug 报告已保存，部分最小化结果保存在 `minimized_partial/` 目录。如需彻底修复，建议使用 AddressSanitizer 重新编译定位根因。

### Q: TxCheck 模式对 PostgreSQL 报告大量 G1c 异常？
**A:** v1.0.18 已修复。PostgreSQL 默认隔离级别为 READ COMMITTED，G1c 在此级别下是合法行为。修复后 `dut_libpq` 在连接时设置 `REPEATABLE READ` 隔离级别。

### Q: Cross 模式误报率高（EET oracle 报告差异）？
**A:** v1.0.18 已添加 EET 空结果过滤。当等价变换后的 SELECT 返回空结果而原始 SELECT 有结果时，判定为变换失败（通常是方言不兼容）而非真实 Bug，跳过 EET oracle。

### Q: 如何分析 Smoke 模式的 GraphML dump？
**A:** 使用 `--dump-all-graphs` 后，每条 SQL 的 AST 会保存为 `dbfuzz-N.xml` 文件。可使用 yEd、Gephi 或 Python 的 `networkx` 库加载 GraphML 文件进行可视化分析。


---

## 13. 架构概览

### 13.1 模块结构

```
dbfuzz
├── core        — 共享工具（随机数、阻抗反馈、日志、Schema 管理、已知错误过滤）
├── grammar     — SQL 语句 AST 生成（SELECT/INSERT/UPDATE/DELETE/DDL/分区/SAVEPOINT/LOCK）
├── expr        — 表达式 AST（常量、列引用、函数调用、布尔表达式、窗口函数、JSON、CAST、INTERVAL）
├── schema      — Schema 提取 + DBMS 驱动（dut_base 接口 + 各 DBMS 实现 + feature flags）
├── txcheck     — 事务测试（插桩、依赖分析、异常检测、最小化）
├── eet         — 逻辑 Bug 测试（QCN 测试器：select/update/delete/cte/insert-select）
├── cross       — 跨模式比较测试（事务 + EET 三路比较 + 最小化 + SIGSEGV 容错）
└── smoke       — 崩溃检测（sqlsmith 原生运行时：ROLLBACK/BEGIN 事务包裹、阻抗反馈、GraphML dump）
```

### 13.2 CMake 对象库

| 库名 | 目录 | 职责 |
|------|------|------|
| `core_objs` | `src/core/` | 共享工具（random, dump, impedance_feedback, known_errors） |
| `grammar_objs` | `src/grammar/` | SQL 生成（SELECT/INSERT/UPDATE/DELETE/DDL/分区/CTE/SAVEPOINT） |
| `expr_objs` | `src/expr/` | 表达式 AST（constants, columns, functions, boolean, JSON, CAST, INTERVAL） |
| `schema_objs` | `src/schema/` | Schema + 驱动（MySQL/PG/TiDB/MariaDB/OceanBase/Yugabyte/Cockroach/GaussDB/SQLite/ClickHouse） |
| `txcheck_objs` | `src/txcheck/` | 事务测试（instrumentor, dependency_analyzer, transaction_test） |
| `eet_objs` | `src/eet/` | 逻辑 Bug 测试（qcn_select/update/delete/cte/insert_select_tester） |
| `cross_objs` | `src/cross/` | 跨模式测试（cross_tester, cross_run, minimize） |
| `smoke_objs` | `src/smoke/` | 崩溃检测（smoke_run, 阻抗反馈, GraphML dump） |

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

**EET / TxCheck / Cross 模式**采用 **fork-based** 循环：

```
主进程
├── fork() → 子进程 1：测试轮次 1
├── fork() → 子进程 2：测试轮次 2
├── ...
└── fork() → 子进程 N：测试轮次 N
```

每个子进程独立执行完整的测试流程（Schema 生成 → SQL 生成 → 执行 → 比较）。如果子进程检测到 Bug，调用 `abort()` 终止自身，主进程记录 Bug 信息后继续下一轮。

**Cross 模式**额外支持子进程崩溃容错：如果 `minimize()` 中发生 SIGSEGV，子进程安装信号处理器优雅退出（exit code 2），父进程继续下一轮测试。

**Smoke 模式**采用**单进程长连接**循环（复刻 sqlsmith 行为）：

```
单进程
├── 连接 DBMS → 提取 Schema
├── 循环：ROLLBACK; BEGIN; <stmt>; ROLLBACK;
├── 连接断开 → 双层恢复循环（内层重连 + 外层重建 Schema）
└── 持续直到 --max-queries 或手动终止
```

### 13.5 门禁验证

分阶段验证构建和功能：

```bash
bash script/gate/run_gate.sh 0   # Phase 0: 项目骨架
bash script/gate/run_gate.sh 1   # Phase 1: core + grammar + expr + schema
bash script/gate/run_gate.sh 2   # Phase 2: TxCheck + EET 集成
bash script/gate/run_gate.sh 3   # Phase 3: Cross 跨库测试
```

---

## 14. 集成测试结果

### 14.1 v1.0.18 全模式集成测试（2026-06-11）

在 MySQL 8.4.8 + PostgreSQL 18.3 上完成的全模式集成测试：

| # | 模式 | DBMS | 时长 | 结果 | Bug |
|---|------|------|------|------|-----|
| 1 | Smoke | MySQL 8.4.8 | 5 min | ✅ PASS | 0 |
| 2 | Smoke | PostgreSQL 18.3 | 5 min | ✅ PASS | 0 |
| 3 | EET | MySQL 8.4.8 | 5.2 min | ✅ PASS | 0 |
| 4 | EET | PostgreSQL 18.3 | 5.5 min | ✅ PASS | 0 |
| 5 | TxCheck | MySQL 8.4.8 | 5 min | ✅ PASS | 0 |
| 6 | TxCheck | PostgreSQL 18.3 | 5 min | ✅ PASS | 0 |
| 7 | Cross | MySQL+PG | 5 min | ✅ PASS | 0 |

**总计**: 7/7 模式全部通过，0 误报，0 崩溃。

### 14.2 测试过程中发现并修复的工具问题

集成测试过程中发现并修复了 5 个工具级 Bug：

| # | 问题 | 修复方案 | 影响 |
|---|------|---------|------|
| 1 | PostgreSQL 未设置隔离级别 | 添加 REPEATABLE READ（构造函数 + reset + reset_to_backup） | TxCheck PG 误报 10→0 |
| 2 | Cross EET 变换返回空结果 | 空结果时跳过 EET oracle | Cross 误报 8→0 |
| 3 | TxCheck 高错误率产生噪声依赖 | >50% 失败率跳过测试用例 | 虚假异常消除 |
| 4 | dependency_analyzer stoi() 解析异常 | 过滤 stoi/stol/stod 异常 | MySQL 误报 7→0 |
| 5 | Cross minimize() SIGSEGV | SIGSEGV 信号处理器 + 优雅退出 | 测试不再中断 |

### 14.3 版本演进

从 v1.0.11（用户指南首版）到 v1.0.18 的主要变更：

| 版本 | 关键特性 |
|------|---------|
| v1.0.12 | Smoke 模式、feature flags 机制、PG 窗口帧/量化比较/JSON 运算符、MySQL UPSERT |
| v1.0.13 | RNG 状态序列化/反序列化、GraphML AST dump、known.txt 错误过滤 |
| v1.0.14 | MySQL 动态类型别名、REPLACE/EXPLAIN/表维护语句、REGEXP/SOUNDS LIKE |
| v1.0.15 | CAST/INTERVAL/JSON 表达式、WITH RECURSIVE CTE、SAVEPOINT、LOCK TABLES |
| v1.0.16 | MySQL/PostgreSQL 分区表（RANGE/LIST/HASH/KEY）、分区管理语句 |
| v1.0.17 | Cross 模式 SIGSEGV 容错、目录创建顺序修复 |
| v1.0.18 | PostgreSQL 隔离级别修复、EET 空结果过滤、错误率阈值、stoi 异常过滤 |

详细变更记录见 [Release Notes](release_notes.md)。

