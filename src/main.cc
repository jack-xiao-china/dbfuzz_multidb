#include <iostream>
#include <string>
#include <map>
#include <regex>

#include "core/dbms_info.hh"

using namespace std;

// Mode constants
#define MODE_EET     1
#define MODE_TXCHECK 2
#define MODE_CROSS   3

// Forward declarations for mode-specific run functions
extern void eet_run(dbms_info& d_info, map<string, string>& options);
extern void txcheck_run(dbms_info& d_info, map<string, string>& options);
extern void cross_run(dbms_info& d_info, map<string, string>& options);

static void print_usage() {
    cerr <<
    "Usage: dbfuzz --mode=<mode> --<dbms>-db=<dbname> [options]" << endl <<
    endl <<
    "Modes:" << endl <<
    "    --mode=txcheck     Transaction bug detection (TxCheck)" << endl <<
    "    --mode=eet         Logic bug detection (EET/QCN)" << endl <<
    "    --mode=cross       Cross-database testing" << endl <<
    endl <<
    "DBMS connections (provide at least one):" << endl <<
    "    --mysql-db=str        MySQL database name" << endl <<
    "    --mysql-port=int      MySQL server port" << endl <<
    "    --mysql-host=str      MySQL server host (default: 127.0.0.1)" << endl <<
    "    --mysql-user=str      MySQL user (default: root)" << endl <<
    "    --mysql-pass=str      MySQL password" << endl <<
    "    --mariadb-db=str      MariaDB database name" << endl <<
    "    --mariadb-port=int    MariaDB server port" << endl <<
    "    --postgres-db=str     PostgreSQL database name" << endl <<
    "    --postgres-port=int   PostgreSQL server port" << endl <<
    "    --postgres-path=str   PostgreSQL installation path (default: /usr/local/pgsql)" << endl <<
    "    --postgres-host=str   PostgreSQL server host (default: localhost)" << endl <<
    "    --postgres-user=str   PostgreSQL user" << endl <<
    "    --postgres-pass=str   PostgreSQL password" << endl <<
    "    --sqlite=file         SQLite database file" << endl <<
    "    --clickhouse-db=str   ClickHouse database name" << endl <<
    "    --clickhouse-port=int ClickHouse server port" << endl <<
    "    --tidb-db=str         TiDB database name" << endl <<
    "    --tidb-port=int       TiDB server port" << endl <<
    "    --oceanbase-db=str    OceanBase database name" << endl <<
    "    --oceanbase-port=int  OceanBase server port" << endl <<
    "    --oceanbase-host=str  OceanBase server host" << endl <<
    "    --yugabyte-db=str     YugabyteDB database name" << endl <<
    "    --yugabyte-port=int   YugabyteDB server port" << endl <<
    "    --yugabyte-host=str   YugabyteDB server host" << endl <<
    "    --cockroach-db=str    CockroachDB database name" << endl <<
    "    --cockroach-port=int  CockroachDB server port" << endl <<
    "    --cockroach-host=str  CockroachDB server host" << endl <<
    "    --gaussdb-m-db=str    GaussDB-M database name" << endl <<
    "    --gaussdb-m-port=int  GaussDB-M server port" << endl <<
    "    --gaussdb-m-host=str  GaussDB-M server host" << endl <<
    "    --gaussdb-m-user=str  GaussDB-M user" << endl <<
    "    --gaussdb-m-pass=str  GaussDB-M password" << endl <<
    "    --gaussdb-a-db=str    GaussDB-A database name" << endl <<
    "    --gaussdb-a-port=int  GaussDB-A server port" << endl <<
    "    --gaussdb-a-host=str  GaussDB-A server host" << endl <<
    "    --gaussdb-a-user=str  GaussDB-A user" << endl <<
    "    --gaussdb-a-pass=str  GaussDB-A password" << endl <<
    endl <<
    "General options:" << endl <<
    "    --seed=int            Random seed (default: random)" << endl <<
    "    --cpu-affinity=int    Set CPU affinity to specific core" << endl <<
    "    --ignore-crash        Ignore crash bugs, continue testing" << endl <<
    endl <<
    "TxCheck options:" << endl <<
    "    --output-or-affect-num=int   Row count limit for generated statements" << endl <<
    "    --reproduce-sql=file         SQL file to reproduce a bug" << endl <<
    "    --reproduce-tid=file         TID file to reproduce a bug" << endl <<
    "    --reproduce-usage=file       Stmt usage file to reproduce a bug" << endl <<
    "    --reproduce-backup=file      Backup file to reproduce a bug" << endl <<
    "    --min                        Minimize the reproduce test case" << endl <<
    endl <<
    "EET options:" << endl <<
    "    --db-test-num=int     Number of QCN tests per database" << endl <<
    "    --db-table-num=int    Number of tables per generated database" << endl <<
    endl <<
    "    --help                Print this help and exit" << endl;
}

int main(int argc, char *argv[])
{
    // Parse command-line arguments using regex (same style as TxCheck/EET)
    map<string, string> options;
    regex optregex("--(\
help|mode|\
seed|cpu-affinity|ignore-crash|\
mysql-db|mysql-port|mysql-host|mysql-user|mysql-pass|\
mariadb-db|mariadb-port|\
postgres-db|postgres-port|postgres-path|postgres-host|postgres-user|postgres-pass|\
sqlite|\
clickhouse-db|clickhouse-port|\
tidb-db|tidb-port|\
oceanbase-db|oceanbase-port|oceanbase-host|\
yugabyte-db|yugabyte-port|yugabyte-host|\
cockroach-db|cockroach-port|cockroach-host|\
gaussdb-m-db|gaussdb-m-port|gaussdb-m-host|gaussdb-m-user|gaussdb-m-pass|\
gaussdb-a-db|gaussdb-a-port|gaussdb-a-host|gaussdb-a-user|gaussdb-a-pass|\
output-or-affect-num|\
reproduce-sql|reproduce-tid|reproduce-usage|reproduce-backup|min|\
db-test-num|db-table-num)(?:=((?:.|\n)*))?");

    for (char **opt = argv + 1; opt < argv + argc; opt++) {
        smatch match;
        string s(*opt);
        if (regex_match(s, match, optregex)) {
            options[string(match[1])] = match[2];
        } else {
            cerr << "Cannot parse option: " << *opt << endl;
            options["help"] = "";
        }
    }

    // Handle --help or parse errors
    if (options.count("help")) {
        print_usage();
        return 0;
    }

    // Validate --mode (required)
    if (!options.count("mode")) {
        cerr << "Error: --mode is required (txcheck|eet|cross)" << endl << endl;
        print_usage();
        return 1;
    }

    string mode_str = options["mode"];
    int mode = 0;
    if (mode_str == "eet")
        mode = MODE_EET;
    else if (mode_str == "txcheck")
        mode = MODE_TXCHECK;
    else if (mode_str == "cross")
        mode = MODE_CROSS;
    else {
        cerr << "Error: unknown mode '" << mode_str << "' (expected txcheck|eet|cross)" << endl;
        return 1;
    }

    // Build dbms_info from options
    dbms_info d_info(options);

    cerr << "-------------Test Info------------" << endl;
    cerr << "Mode: " << mode_str << endl;
    cerr << "Test DBMS: " << d_info.dbms_name << endl;
    cerr << "Test database: " << d_info.test_db << endl;
    cerr << "Test port: " << d_info.test_port << endl;
    cerr << "Test host: " << d_info.host_addr << endl;
    if (options.count("seed"))
        cerr << "Seed: " << options["seed"] << endl;
    cerr << "----------------------------------" << endl;

    // Dispatch to mode-specific entry point
    switch (mode) {
    case MODE_EET:
        eet_run(d_info, options);
        break;
    case MODE_TXCHECK:
        txcheck_run(d_info, options);
        break;
    case MODE_CROSS:
        cross_run(d_info, options);
        break;
    }

    return 0;
}
