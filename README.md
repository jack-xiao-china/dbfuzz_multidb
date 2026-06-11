# dbfuzz — Unified Database Fuzzer

A unified DBMS fuzzing tool combining **TxCheck** (OSDI'23, transaction bug detection), **EET** (OSDI'24, logic bug detection), and **sqlsmith** (crash detection) into a single binary with four testing modes.

**Current Version:** v1.0.18 (2026-06-11)

## Testing Modes

| Mode | Description | Oracle | Best For |
|------|-------------|--------|----------|
| `--mode=txcheck` | Transaction semantic bug detection | Statement-Dependency Graph + topological sort oracle | Isolation level violations (G1a/G1b/G1c/G2/G-SI) |
| `--mode=eet` | Logic bug detection via equivalent expression transformation | Three-valued logic identity | Query correctness bugs |
| `--mode=cross` | Cross-mode: transaction flow + SELECT-only EET transforms | Triple comparison | Cross-DBMS compatibility |
| `--mode=smoke` | High-frequency random SQL + crash detection | Crash/abort detection | Stress testing, crash hunting |

## Supported DBMS

**MySQL Protocol:** MySQL 8.x, MariaDB 10.x, TiDB, OceanBase  
**PostgreSQL Protocol:** PostgreSQL 16+, YugabyteDB, CockroachDB, GaussDB-M, GaussDB-A  
**Other:** SQLite3, ClickHouse

## Quick Start

```bash
# Build (Debian/Ubuntu)
apt-get install -y g++ cmake libboost-regex-dev libpqxx-dev libpq-dev
# Optional: libmysqlclient-dev, libsqlite3-dev

cmake -B build
cmake --build build -j$(nproc)

# Run EET mode (logic bugs)
./build/dbfuzz --mode=eet --mysql-db=testdb --mysql-port=3306

# Run TxCheck mode (transaction bugs)
./build/dbfuzz --mode=txcheck --mysql-db=testdb --mysql-port=3306

# Run Cross mode (cross-database comparison)
./build/dbfuzz --mode=cross --mysql-db=testdb --mysql-port=3306

# Run Smoke mode (crash detection, sqlsmith-compatible)
./build/dbfuzz --mode=smoke --postgres-db=testdb --postgres-port=5432 --verbose
```

## Key Features

- **Comprehensive SQL Generation:** Based on SQLsmith, extended with MySQL/PostgreSQL-specific syntax (JSON, partitions, window functions, CTEs, UPSERT, etc.)
- **Transaction Testing:** Detects G1a, G1b, G1c, G2-item, G-SIa, G-SIb anomalies via dependency graph analysis
- **Logic Bug Detection:** 5 QCN testers (SELECT/UPDATE/DELETE/CTE/INSERT-SELECT) with equivalent expression transforms
- **Cross-DBMS Testing:** Runs same workload on multiple DBMS, diffs results with false-positive filtering
- **Stress Testing:** Smoke mode with impedance feedback, crash detection, GraphML AST dumps
- **Bug Reproduction:** Full reproduction files + automatic minimization for TxCheck/Cross bugs
- **DBMS-Aware:** Feature flags prevent generating unsupported syntax per DBMS

## Project Status

All phases complete and integration-tested:

- **Phase 0** ✅ — Foundation + directory structure + CMake build system
- **Phase 1** ✅ — EET mode integration
- **Phase 2** ✅ — TxCheck mode integration
- **Phase 3** ✅ — Cross-mode testing
- **Phase 4** ✅ — Refinement + Smoke mode + Integration testing

**Integration Test Results (v1.0.18):** 7/7 test modes passed (Smoke×2 + EET×2 + TxCheck×2 + Cross×1), 0 false positives on MySQL 8.4.8 + PostgreSQL 18.3.

## Documentation

- **[User Guide](docs/user-guide.md)** — Complete usage manual (Chinese)
- **[Architecture](docs/dbfuzz-architecture-plan.md)** — Design document
- **[Release Notes](docs/release_notes.md)** — Version history (v1.0.1 → v1.0.18)
- **[Feature Analysis](docs/dbfuzz-features-and-mysql-vs-postgres-analysis.md)** — MySQL vs PostgreSQL feature comparison

## Build Requirements

| Dependency | Required | Purpose |
|-----------|----------|---------|
| libpqxx + libpq | Yes | PostgreSQL/Yugabyte/Cockroach/GaussDB support |
| libmysqlclient | No | MySQL/MariaDB/TiDB/OceanBase support |
| libsqlite3 | No | SQLite support |
| Boost.Regex | No | Falls back to std::regex |

## Architecture

8 CMake object libraries:

```
src/
├── core/       — Shared utilities (random, impedance feedback, logging, dump)
├── grammar/    — SQL statement AST (SELECT/INSERT/UPDATE/DELETE/DDL)
├── expr/       — Expression AST (constants, columns, functions, boolean exprs)
├── schema/     — Schema extraction + DBMS drivers (dut_base interface)
├── txcheck/    — Transaction testing (instrumentation, dependency analysis)
├── eet/        — Logic bug testing (QCN testers)
├── cross/      — Cross-database comparison
└── smoke/      — Crash detection (sqlsmith runtime)
```

## Testing with Docker

```bash
# Start MySQL and run EET test
bash script/run_tests.sh eet mysql

# Start MariaDB and run TxCheck test
bash script/run_tests.sh txcheck mariadb

# Manual Docker control
docker compose --profile mysql up -d
docker compose --profile postgres up -d
```

## License

Based on [SQLsmith](https://github.com/anse1/sqlsmith), [TxCheck](https://github.com/FeedingMyself/TxCheck), and [EET](https://github.com/FeedingMyself/EET).

## Citation

If you use dbfuzz in research, please cite the original papers:

- **TxCheck:** "Detecting Isolation Bugs with Transaction Oracle Construction" (OSDI 2023)
- **EET:** "Detecting Logic Bugs in DBMSes with Equivalent Expression Transformation" (OSDI 2024)
- **SQLsmith:** "Finding bugs in database backends with SQL fuzzing"
