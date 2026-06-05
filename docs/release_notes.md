# Release Notes

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
