#!/bin/bash
# dbfuzz Docker-based test runner
# Usage: ./script/run_tests.sh <mode> [dbms]
# Example: ./script/run_tests.sh eet mysql
#          ./script/run_tests.sh txcheck mariadb
#          ./script/run_tests.sh cross mysql

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
ROUNDS=10

# ---- Argument parsing ----
MODE="${1:-}"
DBMS="${2:-mysql}"

if [ -z "$MODE" ]; then
    echo "Usage: $0 <mode> [dbms]"
    echo "  mode:  txcheck | eet | cross"
    echo "  dbms:  mysql | mariadb | postgres | tidb | clickhouse (default: mysql)"
    exit 1
fi

if [[ ! "$MODE" =~ ^(txcheck|eet|cross)$ ]]; then
    echo "Error: invalid mode '$MODE' (expected txcheck|eet|cross)"
    exit 1
fi

# ---- Map DBMS to docker-compose profile and CLI args ----
declare -A DBMS_PORT=(
    [mysql]=3306 [mariadb]=3307 [postgres]=5432
    [tidb]=4000  [clickhouse]=9000
)
declare -A DBMS_OPT=(
    [mysql]="--mysql-db=testdb --mysql-port"
    [mariadb]="--mariadb-db=testdb --mariadb-port"
    [postgres]="--postgres-db=testdb --postgres-port"
    [tidb]="--tidb-db=testdb --tidb-port"
    [clickhouse]="--clickhouse-db=testdb --clickhouse-port"
)

if [ -z "${DBMS_PORT[$DBMS]+x}" ]; then
    echo "Error: unsupported DBMS '$DBMS'"
    echo "Supported: mysql, mariadb, postgres, tidb, clickhouse"
    exit 1
fi

PORT="${DBMS_PORT[$DBMS]}"
DB_OPT="${DBMS_OPT[$DBMS]}=$PORT"

echo "===== dbfuzz test runner ====="
echo "Mode:   $MODE"
echo "DBMS:   $DBMS (port $PORT)"
echo "Rounds: $ROUNDS"
echo ""

# ---- Start container ----
echo "[1/5] Starting $DBMS container..."
docker compose -f "$PROJECT_DIR/docker-compose.yml" up -d "$DBMS"

# ---- Wait for DBMS readiness ----
echo -n "[2/5] Waiting for $DBMS to be ready"
MAX_WAIT=60
for i in $(seq 1 $MAX_WAIT); do
    case "$DBMS" in
        mysql)
            docker compose -f "$PROJECT_DIR/docker-compose.yml" exec -T mysql \
                mysqladmin ping -h127.0.0.1 -uroot -pyour_password --silent 2>/dev/null && break
            ;;
        mariadb)
            docker compose -f "$PROJECT_DIR/docker-compose.yml" exec -T mariadb \
                mariadb-admin ping -h127.0.0.1 -uroot -pyour_password --silent 2>/dev/null && break
            ;;
        postgres)
            docker compose -f "$PROJECT_DIR/docker-compose.yml" exec -T postgres \
                pg_isready -U postgres 2>/dev/null && break
            ;;
        tidb)
            docker compose -f "$PROJECT_DIR/docker-compose.yml" exec -T tidb \
                mysql -h127.0.0.1 -P4000 -uroot -e "SELECT 1" 2>/dev/null && break
            ;;
        clickhouse)
            docker compose -f "$PROJECT_DIR/docker-compose.yml" exec -T clickhouse \
                clickhouse-client --query "SELECT 1" 2>/dev/null && break
            ;;
    esac
    echo -n "."
    sleep 2
    if [ "$i" -eq "$MAX_WAIT" ]; then
        echo " TIMEOUT"
        echo "Error: $DBMS did not become ready within ${MAX_WAIT}s"
        docker compose -f "$PROJECT_DIR/docker-compose.yml" down
        exit 1
    fi
done
echo " ready"

# ---- Build dbfuzz ----
echo "[3/5] Building dbfuzz..."
cmake -S "$PROJECT_DIR" -B "$PROJECT_DIR/build" -DCMAKE_BUILD_TYPE=Debug 2>&1 | tail -3
cmake --build "$PROJECT_DIR/build" -j"$(nproc 2>/dev/null || echo 4)"

# ---- Run test ----
echo "[4/5] Running dbfuzz ($MODE mode, $ROUNDS rounds)..."
"$PROJECT_DIR/build/dbfuzz" --mode="$MODE" $DB_OPT 2>&1 | tail -20
EXIT_CODE=${PIPESTATUS[0]}

# ---- Report ----
echo ""
echo "[5/5] Results"
if [ $EXIT_CODE -eq 0 ]; then
    echo "  Status: PASS (exit code 0)"
else
    echo "  Status: FAIL (exit code $EXIT_CODE)"
fi

# Check for found bugs
BUG_COUNT=$(find "$PROJECT_DIR/found_bugs" -name "*.sql" 2>/dev/null | wc -l || echo 0)
echo "  Bugs found: $BUG_COUNT"

# ---- Cleanup ----
echo ""
echo "Stopping $DBMS container..."
docker compose -f "$PROJECT_DIR/docker-compose.yml" down

exit $EXIT_CODE
