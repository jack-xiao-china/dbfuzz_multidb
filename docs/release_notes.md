# Release Notes

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
