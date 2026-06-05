# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

TxCheck (transfuzz) is a C++11 fuzzer for detecting transactional bugs in DBMSs. Built on [SQLsmith](https://github.com/anse1/sqlsmith), it generates random SQL transactions, instruments statements to track data dependencies, builds dependency graphs, and checks for anomaly cycles (G1a, G1b, G1c, G2-item, G-SIa, G-SIb). Accepted by OSDI 2023.

**Supported targets**: MySQL 8.x, MariaDB 10.x, TiDB 5.x.

## Build & Run

### Build (Debian/Ubuntu)
```bash
apt-get install -y g++ build-essential autoconf autoconf-archive libboost-regex-dev
autoreconf -if
./configure
make -j
```

Additional dependencies detected at configure time: `libmysqlclient` (enables TiDB/MySQL/MariaDB support conditionally via `HAVE_TIDB`, `HAVE_MYSQL`, `HAVE_MARIADB` macros).

### Run against a database
```bash
./transfuzz --mysql-db=testdb --mysql-port=3306 --output-or-affect-num=1
./transfuzz --mariadb-db=testdb --mariadb-port=3306 --output-or-affect-num=1
./transfuzz --tidb-db=testdb --tidb-port=4000 --output-or-affect-num=1
```

### Reproduce a found bug (with minimization)
```bash
./transfuzz --mysql-db=testdb --mysql-port=3306 \
    --reproduce-sql=final_stmts.sql \
    --reproduce-tid=final_tid.txt \
    --reproduce-usage=final_stmt_use.txt \
    --reproduce-backup=mysql_bk.sql \
    --min
```

### Docker-based integration testing
```bash
cp script/mysql/* mysql_test/
cd mysql_test && ./build_docker.sh && ./run_test.sh 4
```
See `docs/mysql_test.md`, `docs/mariadb_test.md`, `docs/tidb_test.md` for per-DBMS instructions.

**No unit tests exist** — validation is done via the fuzzer itself and Docker integration tests.

## Architecture

### Core pipeline (per fuzzing round)
1. **`transfuzz.cc`** — entry point, argument parsing, orchestrates rounds
2. **`transaction_test.cc`** — generates test cases (multi-transaction schedules), manages blocking, runs oracles
3. **`instrumentor.cc`** — transforms generated SQL into instrumented statements (adds version-set reads, overwrite judges) to track WR/RW/WW dependencies
4. **`dependency_analyzer.cc`** — builds dependency graph from instrumented outputs, detects cycles (G1a/G1b/G1c/G2-item/G-SIa/G-SIb anomalies), topological sort, test-case minimization
5. **`general_process.cc`** — hash/comparison utilities, database reset/backup, reproduce & minimize routines

### Database abstraction
- **`dut.hh`** → `dut_base` abstract interface: `test()`, `reset()`, `backup()`, `reset_to_backup()`, `get_content()`, commit/abort/begin statements
- **`mysql.cc/.hh`**, **`mariadb.cc/.hh`**, **`tidb.cc/.hh`** — concrete implementations using `libmysqlclient` (async APIs differ: `mysql_real_query_nonblocking` for MySQL, `mysql_real_query_start` for MariaDB)

### SQL generation (from SQLsmith)
- **`relmodel.cc/.hh`** — relational model types (columns, tables, scopes)
- **`schema.cc/.hh`** — extracts schema from live DB, feeds into generation
- **`grammar.cc/.hh`** — AST productions for SQL statement generation
- **`expr.cc/.hh`** — expression generation
- **`prod.cc/.hh`** — base class for all AST nodes/productions
- **`random.cc/.hh`** — random number utilities

### Conditional compilation
Database support is enabled via autotools checks on `libmysqlclient` symbols:
- `mysql_real_query_nonblocking` detected → `DUT_MYSQL` condition → compiles `mysql.cc`, defines `HAVE_MYSQL`
- `mysql_real_query_start` detected → `DUT_MARIADB` condition → compiles `mariadb.cc`, defines `HAVE_MARIADB`
- `mysql_init` detected → `DUT_TIDB` condition → compiles `tidb.cc`, defines `HAVE_TIDB`

These are `AM_CONDITIONAL` in `configure.ac` and control `Makefile.am` source inclusion + `AM_CPPFLAGS`.

### Key data structures
- **`stmt_usage`** (`instrumentor.hh`) — classifies each statement: `SELECT_READ`, `UPDATE_WRITE`, `INSERT_WRITE`, `DELETE_WRITE`, `BEFORE_WRITE_READ`, `AFTER_WRITE_READ`, `VERSION_SET_READ`
- **`dependency_type`** (`dependency_analyzer.hh`) — edge types in dependency graph: `WRITE_READ`, `WRITE_WRITE`, `READ_WRITE`, `START_DEPEND`, `VERSION_SET_DEPEND`, `OVERWRITE_DEPEND`, etc.
- **`stmt_id`** — identifies a statement by `(txn_id, stmt_idx_in_txn)` pair
- **`operate_unit`** — tracks individual row operations with hash for dependency analysis

## Code Style Notes

- C++11 standard, `using namespace std` convention throughout
- Header files use `.hh` extension, implementations use `.cc`
- Headers include `"config.h"` first (autoconf-generated)
- Conditional DBMS code uses `#ifdef HAVE_MYSQL` / `#ifdef HAVE_TIDB` / `#ifdef HAVE_MARIADB` guards (see `general_process.hh`)
- pthread-based concurrency (mutex/cond for timeout handling)
- Bug output goes to `found_bugs/` directory with SQL files, TID files, usage files, and backup files

## Build System Details

GNU Autotools: `configure.ac` → `autoreconf -if` → `./configure` → `make`. Version tracked in `configure.ac` (currently 1.2.1). Git revision embedded via `CONFIG_GIT_REVISION` m4 macro. `Makefile.local` can override flags (excluded in `.gitignore`).

## Adding a New DBMS Target

1. Create `newdb.hh` and `newdb.cc` implementing `dut_base` interface
2. Add a `schema_newdb` class inheriting from `schema`
3. Add conditional in `configure.ac` (detect client library, add `AM_CONDITIONAL`)
4. Add conditional in `Makefile.am` (include `.cc`, add `AM_CPPFLAGS` define)
5. Add `#ifdef HAVE_NEWDB` guard in `general_process.hh` and `general_process.cc`
6. Add CLI options in `transfuzz.cc` and `dbms_info.cc`
7. Create Docker test scripts in `script/newdb/`
8. Add test docs in `docs/newdb_test.md`