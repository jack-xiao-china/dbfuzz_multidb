# dbfuzz — Unified Database Fuzzer

A unified DBMS fuzzing tool combining **TxCheck** (OSDI'23, transaction bug detection) and **EET** (OSDI'24, logic bug detection) into a single binary with three testing modes.

## Modes

| Mode | Description | Oracle |
|------|-------------|--------|
| `--mode=txcheck` | Transaction semantic bug detection | Statement-Dependency Graph + topological sort oracle |
| `--mode=eet` | Logic bug detection via equivalent expression transformation | Three-valued logic identity |
| `--mode=cross` | Cross-mode: transaction flow + SELECT-only EET transforms | Triple comparison |

## Supported DBMS

MySQL 8.x, MariaDB 10.x, PostgreSQL, SQLite, ClickHouse, TiDB, OceanBase, YugabyteDB, CockroachDB

## Build (Debian/Ubuntu)

```bash
apt-get install -y g++ cmake libboost-regex-dev libpqxx-dev libpq-dev
# Optional: libmysqlclient-dev, libsqlite3-dev

cmake -B build
cmake --build build -j$(nproc)
```

## Usage

```bash
# EET mode (logic bugs)
./build/dbfuzz --mode=eet --mysql-db=testdb --mysql-port=3306

# TxCheck mode (transaction bugs)
./build/dbfuzz --mode=txcheck --mysql-db=testdb --mysql-port=3306

# Cross mode
./build/dbfuzz --mode=cross --mysql-db=testdb --mysql-port=3306
```

## Architecture

See [docs/dbfuzz-architecture-plan.md](docs/dbfuzz-architecture-plan.md) for the full design document (v3).

## Project Status

- **Phase 0** ✅ — Foundation + directory structure + CMake build system
- **Phase 1** 🔧 — EET mode integration
- **Phase 2** ⏳ — TxCheck mode integration
- **Phase 3** ⏳ — Cross-mode testing
- **Phase 4** ⏳ — Refinement + optimization

## License

Based on [SQLsmith](https://github.com/anse1/sqlsmith), [TxCheck](https://github.com/FeedingMyself/TxCheck), and [EET](https://github.com/FeedingMyself/EET).
