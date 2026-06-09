#ifndef DBMS_INFO_HH
#define DBMS_INFO_HH


#include <string>
#include <map>
#include <iostream>

using namespace std;

enum test_mode {
    MODE_TXCHECK,
    MODE_EET,
    MODE_CROSS
};

struct dbms_info {
    string dbms_name;
    string test_db;
    string inst_path;
    int test_port;
    int ouput_or_affect_num;
    bool can_trigger_error_in_txn;
    string host_addr;
    string dbms_user;
    string dbms_pass;

    test_mode mode;
    int db_test_num;
    int db_table_num;
    bool require_pkey_wkey;
    bool ignore_crash;

    dbms_info(map<string,string>& options);
    dbms_info() {
        dbms_name = "";
        test_db = "";
        inst_path = "";
        test_port = 0;
        ouput_or_affect_num = 0;
        can_trigger_error_in_txn = false;
        host_addr = "";
        dbms_user = "";
        dbms_pass = "";
        mode = MODE_EET;
        db_test_num = 50;
        db_table_num = 0;
        require_pkey_wkey = false;
        ignore_crash = false;
    };
};

#endif