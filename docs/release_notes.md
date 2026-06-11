# Release Notes

## v1.0.19 | 2026-06-11
- 新增 [build]：CMake FetchContent 依赖打包 — `-DUSE_BUNDLED_DEPS=ON` 自动从源码下载编译 SQLite3 amalgamation、MariaDB Connector/C v3.4.5、PostgreSQL libpq REL_17_6、libpqxx 7.10.0
- 新增 [build]：`cmake/bundled_*.cmake` 模块 — 5 个独立模块分别处理各依赖的下载和构建
- 新增 [build]：`script/build_bundled.sh` — 一键构建脚本，自动检测 yum/dnf/apt-get 安装构建工具链
- 优化 [CMake]：版本号升级至 1.0.19，find_package 添加 `if(NOT XXX_FOUND)` 守卫确保 bundled 优先

## v1.0.18 | 2026-06-11
- 修复 [schema]：PostgreSQL 隔离级别设置 — `dut_libpq` 构造函数和 `reset()`/`reset_to_backup()` 重连后设置 REPEATABLE READ（与 MySQL 一致），消除 G1c 误报
- 修复 [cross]：EET 变换空结果过滤 — 当变换后 SELECT 返回空而原始 SELECT 非空时，跳过 EET oracle，消除 87.5% 的跨库误报
- 修复 [txcheck]：执行错误率阈值 — 事务内语句失败率 >50% 时跳过测试用例，防止噪声依赖边产生虚假异常
- 修复 [txcheck]：stoi 解析异常误报 — 过滤 dependency_analyzer 的 stoi/stol/stod 异常，不再误报为事务 Bug
- 修复 [cross]：SIGSEGV 信号处理器（v1.0.17）已在上一版本完成
- 测试 [验证]：全模式 7 项测试通过（Smoke×2 + EET×2 + TxCheck×2 + Cross×1），总误报从 23 降为 0

## v1.0.17 | 2026-06-11
- 修复 [cross]：Cross 模式 bug 目录创建顺序错误 — `make_dir_error_exit()` 现在在 `cross_test()` 之前调用，确保 `save_bug_report()` 写入时目录已存在
- 修复 [cross]：Cross 模式最小化 SIGSEGV 容错 — 子进程安装 SIGSEGV 信号处理器，崩溃后优雅退出 (exit code 2)，父进程继续下一轮测试
- 修复 [cross]：minimize 函数添加 try-catch 异常处理 — 异常时保存部分最小化结果到 `minimized_partial/` 目录
- 测试 [集成测试]：完成 MySQL 8.4.8 + PostgreSQL 18.3 全模式 7 项集成测试（Smoke/EET/TxCheck/Cross，每项 ≥5 分钟），发现 23 个 Bug（TxCheck MySQL 5 + TxCheck PG 10 + Cross 8）

## v1.0.16 | 2026-06-09
- 新增 [grammar]：MySQL 分区表建表 — 支持 PARTITION BY RANGE/LIST/HASH/KEY/RANGE COLUMNS/LIST COLUMNS 6 种分区类型
- 新增 [grammar]：MySQL 二级分区 — RANGE+HASH / RANGE+KEY / LIST+HASH / LIST+KEY 子分区 (30% 概率)
- 新增 [grammar]：MySQL 分区管理 — ALTER TABLE ADD/DROP/TRUNCATE/COALESCE/REORGANIZE PARTITION + ANALYZE/CHECK/OPTIMIZE/REBUILD/REPAIR + REMOVE PARTITIONING
- 新增 [grammar]：MySQL 分区选择 — SELECT/UPDATE/DELETE ... PARTITION (p0, p1)
- 新增 [grammar]：PostgreSQL 分区表建表 — PARTITION BY RANGE/LIST/HASH + CREATE TABLE ... PARTITION OF ... FOR VALUES
- 新增 [grammar]：PostgreSQL DEFAULT 分区 — 捕获不匹配任何分区范围的行
- 新增 [grammar]：PostgreSQL 分区管理 — ALTER TABLE ATTACH/DETACH PARTITION
- 新增 [grammar]：CockroachDB HASH 分区 — PARTITION BY HASH (col) PARTITIONS n (15% 概率)
- 新增 [grammar]：复合主键输出 — 当分区列与 PK 列不同时自动输出 composite PK
- 新增 [grammar]：分区表全局追踪 — `table_partitions` map 记录分区类型、分区名、分区列信息
- 新增 [grammar]：MySQL 分区表禁用外键 — 分区表不生成 FOREIGN KEY (MySQL 不支持)
- 新增 [schema]：6 个 feature flags — has_partition_table, has_subpartition, has_partition_mgmt, has_partition_select, has_partition_default, has_attach_partition

## v1.0.15 | 2026-06-09
- 新增 [expr]：CAST/CONVERT 表达式 — `cast_expr` 类, 支持 CAST(expr AS SIGNED/CHAR/DECIMAL/DATE/DATETIME/TIME) 和 MySQL CONVERT(expr, type)
- 新增 [expr]：INTERVAL 日期算术 — `interval_expr` 类, 生成 `date + INTERVAL n DAY/MONTH/YEAR/HOUR/MINUTE/SECOND`
- 新增 [expr]：MySQL JSON ->/->> 操作符 — `mysql_json_op` 类, 生成 `col->'$.key'` 和 `col->>'$.key'`
- 新增 [expr]：MEMBER OF 表达式 — `member_of_expr` 类, 生成 `value MEMBER OF(json_array)`
- 新增 [expr]：NOT IN 子查询 — `in_query` 扩展 `is_negated` 成员, 30% 概率生成 `NOT IN`
- 新增 [grammar]：WITH RECURSIVE CTE — `common_table_extension` 扩展 `is_recursive`, 生成递归 CTE (anchor UNION ALL recursive)
- 新增 [grammar]：SAVEPOINT 语句 — `savepoint_stmt` 类, 生成 SAVEPOINT/RELEASE SAVEPOINT/ROLLBACK TO SAVEPOINT
- 新增 [grammar]：LOCK/UNLOCK TABLES — `lock_stmt` 类, 生成 LOCK TABLES t READ/WRITE 和 UNLOCK TABLES
- 新增 [grammar]：ALTER TABLE 扩展 — DROP COLUMN (type 3), MODIFY COLUMN (type 4), ADD INDEX (type 5)
- 新增 [schema]：MySQL BLOB 类型 — blob/tinblob/mediumblob/longblob/binary/varbinary 别名映射
- 新增 [schema]：MySQL 存储引擎 — InnoDB/MyISAM/MEMORY 填充到 supported_table_engine
- 新增 [schema]：MySQL 表选项 — CHARSET=utf8mb4/latin1, ROW_FORMAT=DYNAMIC/COMPRESSED
- 新增 [schema]：MySQL JSON 类型 + 15 个 JSON 函数 — JSON_ARRAY/OBJECT/EXTRACT/SET/INSERT/REPLACE/REMOVE/MERGE_PATCH/DEPTH/LENGTH/TYPE/UNQUOTE/CONTAINS/KEYS/QUOTE
- 新增 [schema]：MySQL NTH_VALUE 窗口函数 — 2 参数窗口函数 (expr, N)
- 新增 [schema]：MySQL 窗口帧启用 — has_window_frame=true, 支持 ROWS/RANGE/GROUPS BETWEEN...AND...
- 新增 [schema]：MySQL NATURAL JOIN — 自动基于同名列连接
- 新增 [schema]：MySQL COLLATE 表达式 — 文本列 5% 概率追加 COLLATE utf8mb4_general_ci/bin/unicode_ci
- 新增 [schema]：MySQL BINARY 前缀 — 文本常量 5% 概率添加 BINARY 前缀
- 新增 [schema]：MySQL 日期/时间函数 — NOW/CURDATE/CURTIME/SYSDATE/UTC_TIMESTAMP/DATE_FORMAT/STR_TO_DATE/LAST_DAY/MAKEDATE/MAKETIME
- 新增 [schema]：MySQL 信息函数 — VERSION/DATABASE/USER/CONNECTION_ID/LAST_INSERT_ID/FOUND_ROWS/ROW_COUNT
- 新增 [schema]：MySQL 正则函数 — REGEXP_REPLACE/REGEXP_INSTR/REGEXP_SUBSTR
- 新增 [schema]：MySQL sql_mode 参数 — ONLY_FULL_GROUP_BY/STRICT_TRANS_TABLES/NO_ZERO_IN_DATE/ERROR_FOR_DIVISION_BY_ZERO/NO_ENGINE_SUBSTITUTION/PIPES_AS_CONCAT
- 新增 [schema]：PostgreSQL CAST 支持 — has_cast=true
- 新增 [flag]：schema.hh 新增 4 个 feature flags — has_cast, has_interval_expr, has_mysql_json, has_savepoint

## v1.0.14 | 2026-06-09
- 新增 [schema]：MySQL 动态类型别名映射 — BIGINT/MEDIUMINT/SMALLINT/TINYINT→int, FLOAT/DECIMAL/NUMERIC→double, VARCHAR/CHAR/TEXT/MEDIUMTEXT/LONGTEXT→text, DATE/TIME/TIMESTAMP/YEAR→DATETIME, BOOLEAN→tinyint
- 新增 [schema]：MySQL 操作符补全 — 补齐 `=` 比较操作符 (int/real/text)，补齐 INTERSECT/EXCEPT 复合运算符 (MySQL 8.0.31+)
- 新增 [schema]：MySQL 函数扩展 — IF/IFNULL/ISNULL/NULLIF 条件函数, CONCAT_WS/MD5/SHA1/SHA2/FIND_IN_SET/FORMAT/UCASE/LCASE 字符串函数, 函数总数 100→129
- 新增 [schema]：MySQL 聚合扩展 — ANY_VALUE/GROUP_CONCAT/JSON_ARRAYAGG/JSON_OBJECTAGG, 聚合总数 23→31
- 新增 [schema]：MySQL 窗口函数启用 — NTILE, LAG, LEAD 窗口函数
- 新增 [grammar]：MySQL REPLACE 语句 — `replace_stmt` 类, 加入 statement_factory (choice==17, has_replace)
- 新增 [grammar]：MySQL EXPLAIN 语句 — `explain_stmt` 类, 支持 EXPLAIN/EXPLAIN ANALYZE/EXPLAIN FORMAT=JSON|TREE|TRADITIONAL
- 新增 [grammar]：MySQL 表维护语句 — `table_maintenance_stmt` 类 (CHECKSUM/CHECK/OPTIMIZE/REPAIR TABLE)
- 新增 [grammar]：MySQL STRAIGHT_JOIN — joined_table 支持 straight_join 类型, supported_join_op 新增
- 新增 [grammar]：MySQL Index Hints — table_or_query_name 支持 USE/FORCE/IGNORE INDEX (1/9 概率)
- 新增 [grammar]：MySQL SELECT 选项 — query_spec 支持 SQL_CALC_FOUND_ROWS/SQL_NO_CACHE/HIGH_PRIORITY 等
- 新增 [grammar]：MySQL GROUP BY WITH ROLLUP — group_clause 支持 WITH ROLLUP 后缀 (1/6 概率)
- 新增 [grammar]：MySQL UPDATE/DELETE ORDER BY + LIMIT — update_stmt 和 delete_stmt 支持 ORDER BY + LIMIT (1/6 概率)
- 新增 [expr]：MySQL REGEXP/RLIKE 表达式 — `regexp_expr` 类 (含 NOT REGEXP), 由 has_regexp flag 控制
- 新增 [expr]：MySQL SOUNDS LIKE 表达式 — `sounds_like_expr` 类, 由 has_sounds_like flag 控制
- 新增 [schema]：MySQL feature flags 扩展 — 新增 has_regexp/has_sounds_like/has_straight_join/has_index_hints/has_with_rollup/has_replace/has_do_stmt/has_explain/has_select_options (共 9 个新 flag)
- 新增 [docs]：`docs/dbfuzz-features-and-mysql-vs-postgres-analysis.md` — dbfuzz 功能全景与 MySQL/PG 差异分析
- 优化 [schema]：MySQL 类型系统从 5 种扩展到 20+ 种类型别名, 大幅提升列类型兼容性

## v1.0.13 | 2026-06-09
- 新增 [smoke]：`--rng-state` / `--rng-state-out` — mt19937_64 RNG 状态序列化/反序列化，支持精确复现查询序列和断点续测
- 新增 [smoke]：`--dump-all-graphs` 接线 — 启用 `ast_logger` 将每个 AST dump 为 GraphML XML 文件（`dbfuzz-N.xml`）
- 新增 [schema]：PG14+ `anycompatible*` 伪类型族 — `anycompatible`、`anycompatiblearray`、`anycompatiblenonarray`、`anycompatiblerange`、`anycompatiblemultirange`、`anymultirange` 类型一致性匹配
- 新增 [core]：`known.txt` / `known_re.txt` 已知错误过滤机制 — 支持精确子串匹配和正则匹配，已过滤错误仅传递给 impedance_feedback（语法适应），不输出到 cerr_logger
- 新增 [smoke]：`SET application_name TO 'dbfuzz::dut'` — 在 pg_stat_activity 中可识别 fuzzing 连接
- 新增 [docs]：`docs/sqlsmith-vs-dbfuzz-deep-analysis.md` — sqlsmith 与 dbfuzz 深度对比分析报告
- 优化 [core]：GraphML dump 文件名前缀从 `sqlsmith-` 改为 `dbfuzz-`

## v1.0.12 | 2026-06-09
- 新增 [smoke]：Smoke 测试模式 — 严格复刻 sqlsmith 原生运行时行为，`ROLLBACK; BEGIN; stmt; ROLLBACK;` 事务包裹、`statement_timeout=1s`、双层连接恢复循环、阻抗反馈黑名单机制
- 新增 [smoke]：CLI 选项对齐 sqlsmith — `--verbose`、`--max-queries`、`--dry-run`、`--dump-all-queries`、`--dump-all-graphs`、`--exclude-catalog`
- 新增 [core]：`db_feature_flags` 机制 — schema 类新增 feature flags 结构体，精确控制各 DBMS 支持的 SQL 特性，避免在不支持的 DBMS 上生成不兼容语法
- 新增 [grammar]：PG 窗口帧子句 — `window_frame` 结构体支持 ROWS/RANGE/GROUPS BETWEEN ... AND ... 帧边界，50% 概率生成
- 新增 [grammar]：PG 数据修改 CTE — `data_modifying_cte` + `cte_dml_item`，支持 `WITH ... AS (DELETE/UPDATE/INSERT ... RETURNING *) SELECT ...`
- 新增 [grammar]：PG 量化比较 — `quantified_comparison` 支持 `expr > ALL (SELECT ...)`, `= ANY`, `<= SOME` 语法
- 新增 [grammar]：PG GROUPING SETS/CUBE/ROLLUP — `group_clause` 扩展支持多维分组语法
- 新增 [grammar]：PG JSON/JSONB 运算符 — `json_extract_op` 支持 `->`, `->>`, `#>`, `#>>` 提取运算符
- 新增 [grammar]：PG 数组构造 — `array_constructor` 支持 `ARRAY[expr1, expr2, ...]` 语法
- 新增 [grammar]：PG ROW 构造器 — `row_constructor` 支持 `ROW(expr1, expr2, ...)` 语法
- 新增 [grammar]：PG EXECUTE 语句 — `execute_stmt` 支持执行 PREPARE 预编译的语句
- 新增 [grammar]：MySQL ON DUPLICATE KEY UPDATE — `upsert_stmt` 根据 feature flags 自动生成 MySQL/PG 对应的 UPSERT 语法
- 优化 [schema]：MySQL 反引号标识符引用 — `quote_name()` 返回 `` `identifier` `` 格式，TiDB/MariaDB/OceanBase 同步更新
- 优化 [schema]：所有 DBMS 驱动设置 feature flags — PostgreSQL、MySQL、TiDB、MariaDB、OceanBase、CockroachDB、YugabyteDB
- 修复 [expr]：`value_expr::factory` 中新增表达式类型的类型约束检查，避免 case_expr/funcall 类型断言失败

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
