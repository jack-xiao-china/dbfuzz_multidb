# Release Notes

## v1.0.11 | 2026-06-09
- 新增 [schema]：GaussDB 驱动 — 支持 GaussDB-M（MySQL 兼容模式）和 GaussDB-A（Oracle 兼容模式），基于 PostgreSQL 协议（libpq），统一 gaussdb.hh/cc 实现
- 新增 [schema]：GaussDB `reset()` — 使用 PL/pgSQL 批量 DROP TABLE/SEQUENCE/INDEX CASCADE 清理 public schema（GaussDB 不支持 `DROP DATABASE WITH (FORCE)` 且 `DROP DATABASE` 等价于 `DROP SCHEMA`）
- 新增 [schema]：GaussDB SQL 备份/恢复 — `backup()` 复制 `db_setup.sql`，`reset_to_backup()` 通过 libpq 逐条执行 SQL 恢复（不依赖 pg_dump/psql 外部工具）
- 新增 [CLI]：`--gaussdb-m-db/port/host/user/pass` 和 `--gaussdb-a-db/port/host/user/pass` 命令行选项
- 修复 [schema]：MySQL `dut_mysql::backup()` 错误消息误写为 `dut_tidb::backup`（复制粘贴 Bug）
- 修复 [schema]：MySQL 新增 5 个预期错误模式 — `Failed to open the referenced table`、`Statement requires a transform of a subquery`、`Cannot add or update a child row`（FK 约束）、`Failed to add the foreign key constraint`
- 修复 [core]：`interect_test()` / `normal_test()` 中 `abort()` 过于激进 — DDL/DML 生成阶段的随机 SQL 错误不应导致 fuzzer 崩溃，改为仅在 BUG/CONNECTION FAIL 时 abort
- 修复 [eet]：`eet_run.cc` / `cross_main.cc` 中 `generate_database` 的 `abort()` 同上修复
- 修复 [eet]：`qcn_tester.cc` 两处 `abort()` 同上修复 — `execute_query()` 和 `execute_query_with_update()`
- 修复 [schema]：PostgreSQL `backup()` / `reset_to_backup()` — 替换 pg_dump/psql 为 SQL 方式（`db_setup.sql` 复制 + libpq 逐条执行），解决 pg_dump.exe 在 WSL 中挂起的问题
- 修复 [schema]：PostgreSQL `reset_to_backup()` 多行 SQL 语句解析 — 累积行至分号后执行，避免截断 CREATE TABLE 等多行语句
- 新增 [schema]：GaussDB 类型系统兼容 — 处理 `'s'`（set pseudotype）和 `'u'`（undefined）GaussDB 特有 typtype
- 修复 [schema]：GaussDB `oid2type` 查找空指针保护 — 算子/函数/参数类型查找失败时安全跳过，避免段错误
- 修复 [schema]：GaussDB `information_schema.tables` — 使用 `'YES' as is_insertable_into` 替代不存在的列

## v1.0.10 | 2026-06-08
- 新增 [schema]：MySQL 8.x `supported_setting` 配置 — 18 个 optimizer_switch 选项（batch_key_access, block_nested_loop, hash_join 等），EET 模式可随机注入优化器配置
- 修复 [schema]：MySQL `HAVE_MYSQL_NONBLOCK` 版本 `test()` 方法缺失 `env_setting_stmts` 执行，添加同步 SET 语句执行
- 新增 [schema]：PostgreSQL crash 检测 — `test()` 方法中添加 `PQstatus(conn) == CONNECTION_BAD` 检查，连接断开时 throw "BUG!!!"
- 新增 [schema]：PostgreSQL `is_expected_error()` 添加 crash 相关模式（server closed the connection unexpectedly 等）
- 新增 [schema]：YugabyteDB crash 检测 — 同 PostgreSQL 模式，`is_expected_error()` + CONNECTION_BAD 检查
- 新增 [schema]：CockroachDB crash 检测 — `is_expected_error()` 添加 connection reset/refused/broken pipe 模式 + CONNECTION_BAD 检查
- 新增 [schema]：ClickHouse crash 检测 — 添加 Connection refused / Broken pipe / Connection reset / NETWORK_ERROR 等模式
- 完善 [cross]：Bug 报告格式 — `save_bug_report()` 保存 `normal_stmts.sql`、`eet_select_stmts.sql`、`tx_results.out`、`normal_results.out`、`eet_select_results.out`、`db_backup.sql`
- 新增 [cross]：测试用例最小化 — `minimize()` 方法实现两阶段约减：语句级删除（Phase 1）+ SELECT 变换回退（Phase 2），保存最小化结果到 `minimized/` 子目录

## v1.0.9 | 2026-06-08
- 修复 [build]：解决 header guard 冲突 — `core/general_process.hh`、`txcheck/tx_general_process.hh`、`eet/eet_general_process.hh` 原共用 `GENERAL_PROCESS_HH`，导致 include 互相屏蔽；改为唯一 guard `GENERAL_PROCESS_HH` / `TX_GENERAL_PROCESS_HH` / `EET_GENERAL_PROCESS_HH`
- 重构 [txcheck]：`tx_general_process.hh` 改为 `#include "core/general_process.hh"` + txcheck 独有声明，消除 ~100 行重复
- 重构 [eet]：`eet_general_process.hh` 改为 `#include "core/general_process.hh"`，`eet_general_process.cc` 清空所有 28 个重复函数实现（全部由 core 提供）
- 重构 [txcheck]：`tx_general_process.cc` 移除 19 个与 core 重复的函数实现，只保留 txcheck 独有和签名不同的 3 个重载
- 修复 [CMake]：`HAVE_MYSQL_NONBLOCK` 重定义警告 — 从 `COMMON_DEFS` 移除，统一由 `config.h` `#cmakedefine` 管理；同时设置 `HAVE_MYSQL=1`、`HAVE_TIDB=1`、`HAVE_LIBSQLITE3=1` CMake 变量确保 config.h 正确生成
- 修复 [schema]：`postgres.cc` 缺少 `#include <unistd.h>` 导致 `access()` 未声明
- 修复 [txcheck]：`transaction_test.cc` 中 `static get_cur_time_ms()` 与 `general_process.hh` 的 extern 声明冲突，删除 static 定义
- 修复 [txcheck]：`dependency_analyzer.cc` 使用 `#include <dependency_analyzer.hh>` 找不到文件，改为引号形式 `#include "dependency_analyzer.hh"`
- 修复 [txcheck]：`dependency_analyzer.cc` 中 `register` 关键字不符合 C++17，移除
- 修复 [txcheck]：`save_backup_file` 调用签名不匹配（`dut_mysql`/`dut_tidb`/`dut_mariadb` 需要 2 个参数）
- 新增 [schema]：`dut_tidb::use_backup_file()` 方法和实现（TiDB 基于 MySQL 协议）
- 修复 [txcheck]：`txcheck_run.cc` 和 `tx_main.cc` 中 `generate_database(d_info)` 缺少第二参数，改为 `generate_database(d_info, 0)`

## v1.0.8 | 2026-06-05
- 新增 [infra]：添加 `docker-compose.yml`，支持 MySQL/MariaDB/PostgreSQL/TiDB/ClickHouse 容器化测试，使用 Docker profiles 按模式启动
- 新增 [infra]：添加 `script/run_tests.sh` 统一测试运行脚本，支持 `./script/run_tests.sh <mode> [dbms]` 一键构建和测试
- 优化 [docs]：重写 `CLAUDE.md`，完整覆盖三种测试模式、七个 CMake 模块、所有支持的 DBMS、条件编译宏、门禁脚本

## v1.0.7 | 2026-06-05
- 新增 [cross]：实现 cross-mode 测试模块，结合 TxCheck 事务测试与 EET 等价表达式变换
- 新增 [cross]：`cross_tester` 实现完整交叉测试流程：事务执行 → 拓扑排序路径提取 → 非事务顺序执行 → EET 变换执行 → 三路比较 → 重新验证
- 新增 [cross]：`cross_run()` 主循环采用 fork-based 模式（与 eet_run/txcheck_run 一致），支持 `--db-test-num`、`--db-table-num`、`--cpu-affinity`、`--ignore-crash` 选项
- 新增 [cross]：四个检测 oracle：TxCheck 输出 oracle、TxCheck 状态 oracle、EET 输出 oracle、EET 状态 oracle
- 新增 [cross]：`transform_select()` 对 SELECT/CTE 语句的 WHERE、SELECT list、HAVING 子句应用 `equivalent_transform()`
- 修复 [core]：`general_process.hh` 中 `#include <schema.hh>` 和 `#include <dut.hh>` 修正为 `"schema/schema.hh"` 和 `"schema/dut.hh"`，修复跨模块编译问题
- 修复 [txcheck]：`tx_general_process.hh` 同样的 include 路径修正
- 修复 [eet]：`eet_general_process.hh` 同样的 include 路径修正

## v1.0.6 | 2026-06-05
- 合并 [grammar]：TxCheck 事务特性合并入 EET 统一 grammar，`VKEY_IDENT` 硬编码替换为 `schema::get_version_key_name()`，支持 TxCheck 模式使用 `wkey`、EET 模式使用 `vkey`
- 合并 [grammar]：`insert_select_stmt` 新增 `write_op_id++` 及版本键列覆写逻辑（来自 TxCheck）
- 合并 [grammar]：`extern int write_op_id` 和 `SPACE_HOLDER_STMT` 集中声明到 `grammar.hh`，消除多处重复 `extern` 和宏定义

## v1.0.5 | 2026-06-05
- 优化 [schema]：`require_pkey_wkey` 改为 `static` 字段，与 `target_dbms` 保持一致的静态模式
- 优化 [schema]：`get_version_key_name()` 改为 `static` 方法，支持无实例调用
- 优化 [general_process]：`get_schema()` 成功创建 schema 后从 `d_info.require_pkey_wkey` 赋值到 `schema::require_pkey_wkey`

## v1.0.4 | 2026-06-05
- 合并 [dut_base]：`dut_base` 新增 `commit_stmt()`、`abort_stmt()`、`begin_stmt()` 带默认实现，EET 模式 DUT 无需覆写
- 合并 [dut_base]：`dut_base` 新增 `get_process_id()` 带默认空实现，仅 MySQL/MariaDB 覆写用于阻塞检测
- 合并 [mysql]：`mysql.hh` 中 TxCheck 特有方法（`check_whether_block`、`get_process_id`、`fork_db_server`、非阻塞状态变量）加入 `#ifdef HAVE_MYSQL_NONBLOCK` 条件编译
- 合并 [mysql]：`block_test()`、`save_backup_file()`、`use_backup_file()` 保持无条件编译（两种模式共用）
- 合并 [mysql]：`test()` 方法增加 `#ifdef HAVE_MYSQL_NONBLOCK` 分支，非阻塞路径使用 `mysql_real_query_nonblocking`，阻塞回退路径使用 `mysql_real_query` 并支持 `env_setting_stmts`
- 新增 [mysql]：添加 `use_backup_file()` 方法（来自 TxCheck）
- 新增 [mysql]：添加 `get_process_id()` 实现（返回 `mysql_thread_id`，`HAVE_MYSQL_NONBLOCK` 条件编译）
- 优化 [mysql]：`fork_db_server()` 加入 `#ifdef HAVE_MYSQL_NONBLOCK` 条件编译

## v1.0.3 | 2026-06-05
- 修复 [txcheck]：`compare_content()` 循环条件 `iter != a_content.begin()` 改为 `iter != a_content.end()`，修复比较逻辑从未执行的 bug
- 修复 [txcheck]：`build_VS_dependency()` 中 `set_intersection` 第二参数使用 `.begin()` 而非 `.end()` 导致交集始终为空的 bug
- 修复 [txcheck]：`build_OW_dependency()` 中 `set_intersection` 同样的 `.begin()` / `.end()` 错误
- 优化 [txcheck]：`prod.hh` 新增 `WKEY_IDENT` 宏定义
- 优化 [schema]：`schema` 类新增 `require_pkey_wkey` 字段和 `get_version_key_name()` 方法，支持 wkey/vkey 双命名
- 优化 [txcheck]：`instrumentor.cc` 将所有硬编码 `"wkey"` 列名查找替换为 `db_schema->get_version_key_name()`，适配不同模式的版本键命名

## v1.0.2 | 2026-06-05
- 重构 [EET]：将 eet_main.cc 的测试主循环提取到 eet_run.cc 中的 `eet_run()` 函数
- 重构 [EET]：`eet_run()` 接收 `dbms_info` 和 `options map` 参数，不再包含 `main()` 函数
- 重构 [EET]：保留 `minimize_qcn_database()`、`print_output_to_file()`、`print_test_time_info()` 辅助函数
- 重构 [EET]：保留父子进程 pipe 通信机制用于 `dbms_execution_ms` 跟踪
- 重构 [EET]：CPU affinity 设置加入 `#ifdef __linux__` 条件编译
- 更新 [EET]：`eet_main.hh` 添加辅助函数声明和全局变量 extern 声明

## v1.0.1 | 2026-06-05
- 新增 [dbms_info]：统一 dbms_info 模块支持三种测试模式（EET / TxCheck / Cross）
- 新增 [dbms_info]：添加 `test_mode` 枚举（`MODE_TXCHECK`, `MODE_EET`, `MODE_CROSS`）
- 新增 [dbms_info]：添加 EET 模式专用字段 `db_test_num`、`db_table_num`
- 新增 [dbms_info]：添加 `require_pkey_wkey` 字段（TXCHECK/CROSS 模式自动启用）
- 新增 [dbms_info]：添加 `ignore_crash` 标志位
- 新增 [dbms_info]：支持 MariaDB 目标（`mariadb-db` / `mariadb-port` 选项）
- 修复 [dbms_info]：条件编译宏从 `HAVE_LIBMYSQLCLIENT` 更正为 `HAVE_MYSQL`（与 CMakeLists.txt 一致）
