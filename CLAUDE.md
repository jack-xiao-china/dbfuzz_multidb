# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

dbfuzz is a unified C++17 fuzzer for detecting bugs in DBMSs. It combines three testing modes:

- **TxCheck** — transactional bug detection (G1a, G1b, G1c, G2-item, G-SIa, G-SIb anomalies) via dependency graph analysis
- **EET** — logic bug detection using Query Containment Testing (QCN) with SELECT/UPDATE/DELETE/CTE/INSERT-SELECT variants
- **Cross** — cross-database comparison testing that runs the same workload against multiple DBMSs and diffs results

Built on [SQLsmith](https://github.com/anse1/sqlsmith) for SQL generation.

**Supported targets**: MySQL 8.x, MariaDB 10.x, TiDB, OceanBase, PostgreSQL 16, YugabyteDB, CockroachDB, SQLite3, ClickHouse.

## Build & Run

### Build (CMake)
```bash
# Install dependencies (Debian/Ubuntu)
apt-get install -y g++ cmake libboost-regex-dev libpqxx-dev libsqlite3-dev libmysqlclient-dev

# Build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
```

Required: Boost.Regex, libpqxx, PostgreSQL. Optional: libmysqlclient (MySQL/MariaDB/TiDB/OceanBase), SQLite3.

### Run
```bash
# TxCheck mode (transaction bugs)
./build/dbfuzz --mode=txcheck --mysql-db=testdb --mysql-port=3306 --output-or-affect-num=1

# EET mode (logic bugs)
./build/dbfuzz --mode=eet --postgres-db=testdb --postgres-port=5432

# Cross mode (cross-database comparison)
./build/dbfuzz --mode=cross --mysql-db=testdb --mysql-port=3306
```

`--mode` is required. Provide at least one DBMS connection (`--<dbms>-db` + `--<dbms>-port`).

### Reproduce a TxCheck bug
```bash
./build/dbfuzz --mode=txcheck --mysql-db=testdb --mysql-port=3306 \
    --reproduce-sql=final_stmts.sql --reproduce-tid=final_tid.txt \
    --reproduce-usage=final_stmt_use.txt --reproduce-backup=mysql_bk.sql --min
```

### Docker-based testing
```bash
# Start a specific DBMS
docker compose --profile mysql up -d

# Or use the test runner
bash script/run_tests.sh eet mysql
bash script/run_tests.sh txcheck mariadb
```

## Architecture

### Module structure (7 CMake object libraries)

| Module | Directory | Purpose |
|--------|-----------|---------|
| `core_objs` | `src/core/` | Shared utilities: random, impedance feedback, logging, dump, dbms_info, general_process |
| `grammar_objs` | `src/grammar/` | SQL statement AST productions (SELECT, INSERT, UPDATE, DELETE, DDL) |
| `expr_objs` | `src/expr/` | Expression AST: constants, columns, function calls, window functions, boolean expressions |
| `schema_objs` | `src/schema/` | Schema extraction + DBMS drivers (`dut_base` interface + per-DBMS implementations) |
| `txcheck_objs` | `src/txcheck/` | Transaction testing: instrumentation, dependency analysis, cycle detection, minimization |
| `eet_objs` | `src/eet/` | Logic bug testing: QCN testers (select, update, delete, CTE, insert-select), EET orchestration |
| `cross_objs` | `src/cross/` | Cross-database comparison: dual-DBMS execution, result diffing, false-positive filtering |

### Entry point & dispatch
- **`src/main.cc`** — parses `--mode` and DBMS options, builds `dbms_info`, dispatches to mode entry:
  - `eet_run()` (from `src/eet/eet_run.cc`)
  - `txcheck_run()` (from `src/txcheck/txcheck_run.cc`)
  - `cross_run()` (from `src/cross/cross_main.cc`)

### Database abstraction
- **`src/schema/dut.hh`** → `dut_base` abstract interface: `test()`, `reset()`, `backup()`, `reset_to_backup()`, `get_content()`, plus transaction methods (`begin_stmt()`, `commit_stmt()`, `abort_stmt()`)
- Per-DBMS implementations: `mysql.cc`, `mariadb.cc`, `tidb.cc`, `oceanbase.cc`, `postgres.cc`, `yugabyte.cc`, `cockroach.cc`, `sqlite.cc`, `clickhouse.cc`

### SQL generation (from SQLsmith)
- **`src/core/relmodel.hh`** — relational model types (columns, tables, scopes, types)
- **`src/schema/schema.hh`** — extracts schema from live DB, maintains type/operator/routine catalogs
- **`src/grammar/grammar.hh`** — AST productions for SQL statement generation
- **`src/expr/expr.hh`** — expression generation hierarchy
- **`src/core/prod.hh`** — base class for all AST nodes

### Key data structures
- **`test_mode`** (`core/dbms_info.hh`) — enum: `MODE_TXCHECK`, `MODE_EET`, `MODE_CROSS`
- **`dbms_info`** (`core/dbms_info.hh`) — parsed CLI config: dbms_name, test_db, port, mode, options
- **`stmt_usage`** (`txcheck/instrumentor.hh`) — classifies statements: `SELECT_READ`, `UPDATE_WRITE`, `INSERT_WRITE`, `DELETE_WRITE`, `BEFORE_WRITE_READ`, `AFTER_WRITE_READ`, `VERSION_SET_READ`
- **`dependency_type`** (`txcheck/dependency_analyzer.hh`) — graph edges: `WRITE_READ`, `WRITE_WRITE`, `READ_WRITE`, `START_DEPEND`, `VERSION_SET_DEPEND`, `OVERWRITE_DEPEND`
- **`stmt_id`** — identifies a statement by `(txn_id, stmt_idx_in_txn)` pair
- **`operate_unit`** — tracks individual row operations with hash for dependency analysis
- **`schema`** (`schema/schema.hh`) — live DB schema with types, tables, operators, routines, indexes

### Conditional compilation
CMake detects libraries and sets macros via `src/config.h.in` → `build/config.h`:
- `HAVE_MYSQL` / `HAVE_TIDB` — mysqlclient found
- `HAVE_MYSQL_NONBLOCK` — `mysql_real_query_nonblocking` detected (MySQL 8.x async API)
- `HAVE_MARIADB` — `mysql_real_query_start` detected (MariaDB async API)
- `HAVE_LIBSQLITE3` — SQLite3 found

PostgreSQL (libpqxx) is always required. Use `#ifdef HAVE_MYSQL` / `#ifdef HAVE_LIBSQLITE3` guards.

## Code Style

- C++17 standard, `using namespace std` throughout
- Headers: `.hh`, implementations: `.cc`
- Headers include `"config.h"` first (CMake-generated)
- pthread-based concurrency (mutex/cond for timeout handling)
- Bug output goes to `found_bugs/` directory

## Gate Verification

Phase-gated development scripts in `script/gate/`:
```bash
bash script/gate/run_gate.sh 0   # Phase 0: project scaffold
bash script/gate/run_gate.sh 1   # Phase 1: core + grammar + expr + schema
bash script/gate/run_gate.sh 2   # Phase 2: TxCheck + EET integration
bash script/gate/run_gate.sh 3   # Phase 3: cross-database testing
```

## Adding a New DBMS Target

1. Create `src/schema/newdb.hh` and `src/schema/newdb.cc` implementing `dut_base`
2. Add `schema_newdb` class inheriting from `schema`
3. Add CMake detection in root `CMakeLists.txt` (find client library, set `HAVE_NEWDB`)
4. Add conditional source in `src/CMakeLists.txt` schema_objs section
5. Add `#ifdef HAVE_NEWDB` guard in `src/core/general_process.hh` and `.cc`
6. Add CLI options in `src/main.cc` and `src/core/dbms_info.cc`
7. Add Docker service in `docker-compose.yml` with appropriate profile
8. Add gate verification in `script/gate/`
