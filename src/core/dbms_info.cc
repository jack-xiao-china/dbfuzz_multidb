#include "config.h"
#include "core/dbms_info.hh"

dbms_info::dbms_info(map<string,string>& options)
{
    // Parse test mode
    if (options.count("mode")) {
        string m = options["mode"];
        if (m == "txcheck")      mode = MODE_TXCHECK;
        else if (m == "cross")   mode = MODE_CROSS;
        else                     mode = MODE_EET;
    } else {
        mode = MODE_EET;
    }

    // Set require_pkey_wkey based on mode
    require_pkey_wkey = (mode == MODE_TXCHECK || mode == MODE_CROSS);

    if (false) {}
    #ifdef HAVE_LIBSQLITE3
    else if (options.count("sqlite")) {
        dbms_name = "sqlite";
        test_port = 0; // no port
        test_db = options["sqlite"];
        can_trigger_error_in_txn = true;
    }
    #endif

    #ifdef HAVE_MYSQL
    else if (options.count("tidb-db") && options.count("tidb-port")) {
        dbms_name = "tidb";
        test_port = stoi(options["tidb-port"]);
        test_db = options["tidb-db"];
        can_trigger_error_in_txn = true;
    }
    else if (options.count("mysql-db") && options.count("mysql-port")) {
        dbms_name = "mysql";
        test_port = stoi(options["mysql-port"]);
        test_db = options["mysql-db"];
        host_addr = options.count("mysql-host") ? options["mysql-host"] : "127.0.0.1";
        dbms_user = options.count("mysql-user") ? options["mysql-user"] : "root";
        dbms_pass = options.count("mysql-pass") ? options["mysql-pass"] : "";
        can_trigger_error_in_txn = true;
    }
    #ifdef HAVE_MARIADB
    else if (options.count("mariadb-db") && options.count("mariadb-port")) {
        dbms_name = "mariadb";
        test_port = stoi(options["mariadb-port"]);
        test_db = options["mariadb-db"];
        can_trigger_error_in_txn = true;
    }
    #endif
    else if (options.count("oceanbase-db") && options.count("oceanbase-port") && options.count("oceanbase-host")) {
        dbms_name = "oceanbase";
        test_port = stoi(options["oceanbase-port"]);
        test_db = options["oceanbase-db"];
        host_addr = options["oceanbase-host"];
        can_trigger_error_in_txn = true;
    }
    #endif

    else if (options.count("clickhouse-db") && options.count("clickhouse-port")) {
        dbms_name = "clickhouse";
        test_port = stoi(options["clickhouse-port"]);
        test_db = options["clickhouse-db"];
        can_trigger_error_in_txn = false;
    }
    else if (options.count("postgres-db") && options.count("postgres-port")) {
        dbms_name = "postgres";
        test_port = stoi(options["postgres-port"]);
        test_db = options["postgres-db"];
        host_addr = options.count("postgres-host") ? options["postgres-host"] : "localhost";
        dbms_user = options.count("postgres-user") ? options["postgres-user"] : "";
        dbms_pass = options.count("postgres-pass") ? options["postgres-pass"] : "";
        inst_path = options.count("postgres-path") ? options["postgres-path"] : "/usr/local/pgsql";
        can_trigger_error_in_txn = false;
    }
    else if (options.count("yugabyte-db") &&
                    options.count("yugabyte-port") &&
                    options.count("yugabyte-host")) {
        dbms_name = "yugabyte";
        test_port = stoi(options["yugabyte-port"]);
        test_db = options["yugabyte-db"];
        host_addr = options["yugabyte-host"];
        can_trigger_error_in_txn = false;
    }
    else if (options.count("cockroach-db") &&
                    options.count("cockroach-port") &&
                    options.count("cockroach-host")) {
        dbms_name = "cockroach";
        test_port = stoi(options["cockroach-port"]);
        test_db = options["cockroach-db"];
        host_addr = options["cockroach-host"];
        can_trigger_error_in_txn = false;
    }
    else if (options.count("gaussdb-m-db") &&
                    options.count("gaussdb-m-port") &&
                    options.count("gaussdb-m-host")) {
        dbms_name = "gaussdb_m";
        test_port = stoi(options["gaussdb-m-port"]);
        test_db = options["gaussdb-m-db"];
        host_addr = options["gaussdb-m-host"];
        dbms_user = options.count("gaussdb-m-user") ? options["gaussdb-m-user"] : "";
        dbms_pass = options.count("gaussdb-m-pass") ? options["gaussdb-m-pass"] : "";
        can_trigger_error_in_txn = false;
    }
    else if (options.count("gaussdb-a-db") &&
                    options.count("gaussdb-a-port") &&
                    options.count("gaussdb-a-host")) {
        dbms_name = "gaussdb_a";
        test_port = stoi(options["gaussdb-a-port"]);
        test_db = options["gaussdb-a-db"];
        host_addr = options["gaussdb-a-host"];
        dbms_user = options.count("gaussdb-a-user") ? options["gaussdb-a-user"] : "";
        dbms_pass = options.count("gaussdb-a-pass") ? options["gaussdb-a-pass"] : "";
        can_trigger_error_in_txn = false;
    }
    else {
        cerr << "Sorry,  you should specify a dbms and its database, or your dbms is not supported, or you miss arguments" << endl;
        throw runtime_error("Does not define target dbms and db in dbms_info::dbms_info()");
    }

    if (options.count("output-or-affect-num"))
        ouput_or_affect_num = stoi(options["output-or-affect-num"]);
    else
        ouput_or_affect_num = 0;

    // EET mode specific options
    if (options.count("db-test-num"))
        db_test_num = stoi(options["db-test-num"]);
    else
        db_test_num = 50;

    if (options.count("db-table-num"))
        db_table_num = stoi(options["db-table-num"]);
    else
        db_table_num = 0;

    // Ignore crash flag
    if (options.count("ignore-crash"))
        ignore_crash = true;
    else
        ignore_crash = false;

    return;
}