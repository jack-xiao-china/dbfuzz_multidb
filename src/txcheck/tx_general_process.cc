#include "config.h"
#include "txcheck/tx_general_process.hh"

extern int write_op_id;

// ============================================================
// TxCheck-specific overloaded functions (different signatures from core)
// ============================================================

void interect_test(dbms_info& d_info,
                    shared_ptr<prod> (* tmp_statement_factory)(scope *),
                    vector<string>& rec_vec,
                    bool need_affect)
{
    auto schema = get_schema(d_info);
    scope scope;
    schema->fill_scope(scope);

    shared_ptr<prod> gen = tmp_statement_factory(&scope);
    ostringstream s;
    gen->out(s);

    static int try_time = 0;
    try {
        auto dut = dut_setup(d_info);
        auto sql = s.str() + ";";
        int affect_num = 0;
        dut->test(sql, NULL, &affect_num);

        if (need_affect && affect_num <= 0)
            throw runtime_error(string("affect result empty"));

        rec_vec.push_back(sql);

    } catch(std::exception &e) { // ignore runtime error
        string err = e.what();
        cerr << "err: " << e.what() << endl;
        if (err.find("syntax") != string::npos) {
            cerr << "\n" << e.what() << "\n" << endl;
            cerr << s.str() << endl;
        }
        if (try_time >= 128) {
            cerr << "Fail in interect_test() " << try_time << " times, return" << endl;
            throw e;
        }
        try_time++;
        interect_test(d_info, tmp_statement_factory, rec_vec, need_affect);
        try_time--;
    }
}

void normal_test(dbms_info& d_info,
                    shared_ptr<schema>& schema,
                    shared_ptr<prod> (* tmp_statement_factory)(scope *),
                    vector<string>& rec_vec,
                    bool need_affect)
{
    scope scope;
    schema->fill_scope(scope);

    shared_ptr<prod> gen = tmp_statement_factory(&scope);
    ostringstream s;
    gen->out(s);

    static int try_time = 0;
    try {
        auto dut = dut_setup(d_info);
        auto sql = s.str() + ";";
        int affect_num = 0;
        dut->test(sql, NULL, &affect_num);

        if (need_affect && affect_num <= 0)
            throw runtime_error(string("affect result empty"));
        rec_vec.push_back(sql);
    } catch(std::exception &e) { // ignore runtime error
        string err = e.what();
        if (err.find("syntax") != string::npos) {
            cerr << "trigger a syntax problem: " << err << endl;
            cerr << "sql: " << s.str();
        }

        if (err.find("timeout") != string::npos)
            cerr << "time out in normal test: " << err << endl;

        if (err.find("BUG") != string::npos) {
            cerr << "BUG is triggered in normal_test: " << err << endl;
            throw e;
        }

        if (try_time >= 128) {
            cerr << "Fail in normal_test() " << try_time << " times, return" << endl;
            throw e;
        }
        try_time++;
        normal_test(d_info, schema, tmp_statement_factory, rec_vec, need_affect);
        try_time--;
    }
}

int generate_database(dbms_info& d_info)
{
    vector<string> stage_1_rec;
    vector<string> stage_2_rec;

    cerr << "generating database ... ";
    dut_reset(d_info);

    auto ddl_stmt_num = d6() + 1; // at least 2 statements to create 2 tables
    for (auto i = 0; i < ddl_stmt_num; i++)
        interect_test(d_info, &ddl_statement_factory, stage_1_rec, false);

    auto basic_dml_stmt_num = 10 + d6(); // 11-20 statements to insert data
    auto schema = get_schema(d_info); // schema will not change in this stage
    for (auto i = 0; i < basic_dml_stmt_num; i++)
        normal_test(d_info, schema, &basic_dml_statement_factory, stage_2_rec, true);

    dut_backup(d_info);
    cerr << "done" << endl;
    return 0;
}

// ============================================================
// TxCheck-specific unique functions
// ============================================================

int use_backup_file(string backup_file, dbms_info& d_info)
{
    if (false) {}
    #ifdef HAVE_MYSQL
    else if (d_info.dbms_name == "mysql")
        return dut_mysql::use_backup_file(backup_file);
    #endif

    #ifdef HAVE_MARIADB
    else if (d_info.dbms_name == "mariadb")
        return dut_mariadb::use_backup_file(backup_file);
    #endif

    #ifdef HAVE_TIDB
    else if (d_info.dbms_name == "tidb")
        return dut_tidb::use_backup_file(backup_file);
    #endif

    else {
        cerr << d_info.dbms_name << " is not supported yet" << endl;
        throw runtime_error("Unsupported DBMS");
    }
}

void save_current_testcase(vector<shared_ptr<prod>>& stmt_queue,
                            vector<int>& tid_queue,
                            vector<stmt_usage>& usage_queue,
                            string stmt_file_name,
                            string tid_file_name,
                            string usage_file_name)
{
    // save stmt queue
    ofstream mimimized_stmt_output(stmt_file_name);
    for (int i = 0; i < stmt_queue.size(); i++) {
        mimimized_stmt_output << print_stmt_to_string(stmt_queue[i]) << endl;
        mimimized_stmt_output << endl;
    }
    mimimized_stmt_output.close();

    // save tid queue
    ofstream minimized_tid_output(tid_file_name);
    for (int i = 0; i < tid_queue.size(); i++) {
        minimized_tid_output << tid_queue[i] << endl;
    }
    minimized_tid_output.close();

    // save stmt usage queue
    ofstream minimized_usage_output(usage_file_name);
    for (int i = 0; i < usage_queue.size(); i++) {
        minimized_usage_output << usage_queue[i] << endl;
    }
    minimized_usage_output.close();

    return;
}

bool minimize_testcase(dbms_info& d_info,
                        vector<shared_ptr<prod>>& stmt_queue,
                        vector<int>& tid_queue,
                        vector<stmt_usage> usage_queue)
{
    cerr << "Check reproduce..." << endl;
    string original_err;
    auto r_check = reproduce_routine(d_info, stmt_queue, tid_queue, usage_queue, original_err);
    if (!r_check) {
        cerr << "No" << endl;
        return false;
    }
    cerr << "Yes" << endl;

    int max_tid = -1;
    for (auto tid:tid_queue) {
        if (tid > max_tid)
            max_tid = tid;
    }
    int txn_num = max_tid + 1;

    auto final_stmt_queue = stmt_queue;
    vector<int> final_tid_queue = tid_queue;
    vector<stmt_usage> final_usage_queue = usage_queue;

    // txn level minimize
    for (int tid = 0; tid < txn_num; tid++) {
        cerr << "Try to delete txn " << tid << "..." << endl;

        auto tmp_stmt_queue = final_stmt_queue;
        vector<int> tmp_tid_queue = final_tid_queue;
        vector<stmt_usage> tmp_usage_queue = final_usage_queue;

        // delete current tid
        for (int i = 0; i < tmp_tid_queue.size(); i++) {
            if (tmp_tid_queue[i] != tid)
                continue;

            tmp_stmt_queue.erase(tmp_stmt_queue.begin() + i);
            tmp_tid_queue.erase(tmp_tid_queue.begin() + i);
            tmp_usage_queue.erase(tmp_usage_queue.begin() + i);
            i--;
        }

        // adjust tid queue
        for (int i = 0; i < tmp_tid_queue.size(); i++) {
            if (tmp_tid_queue[i] < tid)
                continue;

            tmp_tid_queue[i]--;
        }

        int try_time = 1;
        bool trigger_bug = false;
        while (try_time--) {
            trigger_bug = reproduce_routine(d_info, tmp_stmt_queue, tmp_tid_queue, tmp_usage_queue, original_err);
            if (trigger_bug == true)
                break;
        }
        if (trigger_bug == false)
            continue;

        // reduction succeed
        cerr << "Succeed to delete txn " << tid << "\n\n\n" << endl;

        int pause;
        cerr << "Enter an integer: 0 skip, other save" << endl;
        cin >> pause;
        if (pause == 0)
            continue;

	final_stmt_queue = tmp_stmt_queue;
        final_tid_queue = tmp_tid_queue;
        final_usage_queue = tmp_usage_queue;
        tid--;
        txn_num--;

	save_current_testcase(final_stmt_queue, final_tid_queue, final_usage_queue,
                            "min_stmts.sql", "min_tid.txt", "min_usage.txt");
    }

    // stmt level minimize
    auto stmt_num = final_tid_queue.size();
    auto dut = dut_setup(d_info);
    for (int i = 0; i < stmt_num; i++) {
        cerr << "Try to delete stmt " << i << "..." << endl;

        auto tmp_stmt_queue = final_stmt_queue;
        vector<int> tmp_tid_queue = final_tid_queue;
        vector<stmt_usage> tmp_usage_queue = final_usage_queue;
	    auto tmp_stmt_num = stmt_num;

        // do not delete commit or abort
        auto tmp_stmt_str = print_stmt_to_string(tmp_stmt_queue[i]);
        if (tmp_stmt_str.find(dut->begin_stmt()) != string::npos)
            continue;
        if (tmp_stmt_str.find(dut->commit_stmt()) != string::npos)
            continue;
        if (tmp_stmt_str.find(dut->abort_stmt()) != string::npos)
            continue;

        // do not delete instrumented stmts
        if (tmp_usage_queue[i].is_instrumented == true)
            continue;

        auto original_i = i;

        // delete possible AFTER_WRITE_READ
        if (i + 1 <= tmp_usage_queue.size() && tmp_usage_queue[i + 1] == AFTER_WRITE_READ) {
            tmp_stmt_queue.erase(tmp_stmt_queue.begin() + i + 1);
            tmp_tid_queue.erase(tmp_tid_queue.begin() + i + 1);
            tmp_usage_queue.erase(tmp_usage_queue.begin() + i + 1);
            tmp_stmt_num--;
        }

        // delete the statement
        tmp_stmt_queue.erase(tmp_stmt_queue.begin() + i);
        tmp_tid_queue.erase(tmp_tid_queue.begin() + i);
        tmp_usage_queue.erase(tmp_usage_queue.begin() + i);
        tmp_stmt_num--;
        i--;

        // delete possible BEFORE_WRITE_READ and VERSION_SET_READ
        while (i >= 0 && (tmp_usage_queue[i] == BEFORE_WRITE_READ ||
                            tmp_usage_queue[i] == VERSION_SET_READ)) {
            tmp_stmt_queue.erase(tmp_stmt_queue.begin() + i);
            tmp_tid_queue.erase(tmp_tid_queue.begin() + i);
            tmp_usage_queue.erase(tmp_usage_queue.begin() + i);
            tmp_stmt_num--;
            i--;
        }

        int try_time = 1;
        bool trigger_bug = false;
        while (try_time--) {
            trigger_bug = reproduce_routine(d_info, tmp_stmt_queue, tmp_tid_queue, tmp_usage_queue, original_err);
            if (trigger_bug == true)
                break;
        }
        if (trigger_bug == false) {
            i = original_i;
            continue;
        }

        // reduction succeed
        cerr << "Succeed to delete stmt " << "\n\n\n" << endl;

	    int pause;
        cerr << "Enter an integer: 0 skip, other save" << endl;
        cin >> pause;
        if (pause == 0) {
	        i = original_i;
            continue;
	    }

	    final_stmt_queue = tmp_stmt_queue;
        final_tid_queue = tmp_tid_queue;
        final_usage_queue = tmp_usage_queue;
	    stmt_num = tmp_stmt_num;
        save_current_testcase(final_stmt_queue, final_tid_queue, final_usage_queue,
                            "min_stmts.sql", "min_tid.txt", "min_usage.txt");
    }

    if (final_stmt_queue.size() == stmt_queue.size())
        return false;

    stmt_queue = final_stmt_queue;
    tid_queue = final_tid_queue;
    usage_queue = final_usage_queue;

    save_current_testcase(stmt_queue, tid_queue, usage_queue,
                            "min_stmts.sql", "min_tid.txt", "min_usage.txt");

    return true;
}

bool reproduce_routine(dbms_info& d_info,
                        vector<shared_ptr<prod>>& stmt_queue,
                        vector<int>& tid_queue,
                        vector<stmt_usage> usage_queue,
                        string& err_info)
{
    transaction_test::fork_if_server_closed(d_info);

    transaction_test re_test(d_info);
    re_test.stmt_queue = stmt_queue;
    re_test.tid_queue = tid_queue;
    re_test.stmt_num = re_test.tid_queue.size();
    re_test.stmt_use = usage_queue;

    int max_tid = -1;
    for (auto tid:tid_queue) {
        if (tid > max_tid)
            max_tid = tid;
    }

    re_test.trans_num = max_tid + 1;
    delete[] re_test.trans_arr;
    re_test.trans_arr = new transaction[re_test.trans_num];

    cerr << "txn num: " << re_test.trans_num << ", tid queue: " << re_test.tid_queue.size() << ", stmt queue: " << re_test.stmt_queue.size() << endl;
    if (re_test.tid_queue.size() != re_test.stmt_queue.size()) {
        cerr << "tid queue size should equal to stmt queue size" << endl;
        return 0;
    }

    // init each transaction stmt
    for (int i = 0; i < re_test.stmt_num; i++) {
        auto tid = re_test.tid_queue[i];
        re_test.trans_arr[tid].stmts.push_back(re_test.stmt_queue[i]);
    }

    for (int tid = 0; tid < re_test.trans_num; tid++) {
        re_test.trans_arr[tid].dut = dut_setup(d_info);
        re_test.trans_arr[tid].stmt_num = re_test.trans_arr[tid].stmts.size();
        if (re_test.trans_arr[tid].stmts.empty()) {
            re_test.trans_arr[tid].status = TXN_ABORT;
            continue;
        }

        auto stmt_str = print_stmt_to_string(re_test.trans_arr[tid].stmts.back());
        if (stmt_str.find("COMMIT") != string::npos)
            re_test.trans_arr[tid].status = TXN_COMMIT;
        else
            re_test.trans_arr[tid].status = TXN_ABORT;
    }

    try {
        re_test.trans_test();
        shared_ptr<dependency_analyzer> tmp_da;
        if (re_test.analyze_txn_dependency(tmp_da)) {
            string bug_str = "Find bugs in analyze_txn_dependency";
            cerr << RED << bug_str << RESET << endl;
            if (err_info != "" && err_info != bug_str) {
                cerr << "not same as the original bug" << endl;
                return false;
            }
            err_info = bug_str;
            return true;
        }
        set<stmt_id> empty_deleted_nodes;
        bool delete_flag = false;
        auto longest_stmt_path = tmp_da->topological_sort_path(empty_deleted_nodes, &delete_flag);
        if (delete_flag == true) {
            cerr << "the test case contains cycle and cannot be properly sorted" << endl;
            return false;
        }
        cerr << RED << "stmt path for normal test: " << RESET;
        print_stmt_path(longest_stmt_path, tmp_da->stmt_dependency_graph);

        re_test.normal_stmt_test(longest_stmt_path);
        if (re_test.check_normal_stmt_result(longest_stmt_path, false) == false) {
            string bug_str = "Find bugs in check_normal_stmt_result";
            cerr << RED << bug_str << RESET << endl;
            if (err_info != "" && err_info != bug_str) {
                cerr << "not same as the original bug" << endl;
                return false;
            }
            err_info = bug_str;
            return true;
        }
    } catch (exception &e) {
        string cur_err_info = e.what();
        cerr << "exception captured by test: " << cur_err_info << endl;
        if (cur_err_info.find("INSTRUMENT_ERR") != string::npos)
            return false;
        if (err_info != "" && err_info != cur_err_info) {
            cerr << "not same as the original bug" << endl;
            return false;
        }
        err_info = cur_err_info;
        return true;
    }

    return false;
}

bool check_txn_cycle(dbms_info& d_info,
                        vector<shared_ptr<prod>>& stmt_queue,
                        vector<int>& tid_queue,
                        vector<stmt_usage>& usage_queue)
{
    transaction_test::fork_if_server_closed(d_info);

    transaction_test re_test(d_info);
    re_test.stmt_queue = stmt_queue;
    re_test.tid_queue = tid_queue;
    re_test.stmt_num = re_test.tid_queue.size();
    re_test.stmt_use = usage_queue;

    int max_tid = -1;
    for (auto tid:tid_queue) {
        if (tid > max_tid)
            max_tid = tid;
    }

    re_test.trans_num = max_tid + 1;
    delete[] re_test.trans_arr;
    re_test.trans_arr = new transaction[re_test.trans_num];

    cerr << "txn num: " << re_test.trans_num << endl
        << "tid_queue size: " << re_test.tid_queue.size() << endl
        << "stmt_queue size: " << re_test.stmt_queue.size() << endl;
    if (re_test.tid_queue.size() != re_test.stmt_queue.size()) {
        cerr << "tid queue size should equal to stmt queue size" << endl;
        return false;
    }

    // init each transaction stmt
    for (int i = 0; i < re_test.stmt_num; i++) {
        auto tid = re_test.tid_queue[i];
        re_test.trans_arr[tid].stmts.push_back(re_test.stmt_queue[i]);
    }

    for (int tid = 0; tid < re_test.trans_num; tid++) {
        re_test.trans_arr[tid].dut = dut_setup(d_info);
        re_test.trans_arr[tid].stmt_num = re_test.trans_arr[tid].stmts.size();
        if (re_test.trans_arr[tid].stmts.empty()) {
            re_test.trans_arr[tid].status = TXN_ABORT;
            continue;
        }

        auto stmt_str = print_stmt_to_string(re_test.trans_arr[tid].stmts.back());
        if (stmt_str.find("COMMIT") != string::npos)
            re_test.trans_arr[tid].status = TXN_COMMIT;
        else
            re_test.trans_arr[tid].status = TXN_ABORT;
    }

    try {
        re_test.trans_test();
        shared_ptr<dependency_analyzer> tmp_da;
        re_test.analyze_txn_dependency(tmp_da);
        set<int> cycle_nodes;
        vector<int> sorted_nodes;
        tmp_da->check_txn_graph_cycle(cycle_nodes, sorted_nodes);
        if (!cycle_nodes.empty()) {
            cerr << "Has transactional cycles" << endl;
            return true;
        }
        else {
            cerr << "No transactional cycle" << endl;
            return false;
        }
    } catch (exception &e) {
        string cur_err_info = e.what();
        cerr << "exception captured by test: " << cur_err_info << endl;
    }

    return false;
}

void txn_decycle_test(dbms_info& d_info,
                    vector<shared_ptr<prod>>& stmt_queue,
                    vector<int>& tid_queue,
                    vector<stmt_usage>& usage_queue,
                    int& succeed_time,
                    int& all_time,
                    vector<int> delete_nodes)
{
    transaction_test::fork_if_server_closed(d_info);

    transaction_test re_test(d_info);
    re_test.stmt_queue = stmt_queue;
    re_test.tid_queue = tid_queue;
    re_test.stmt_num = re_test.tid_queue.size();
    re_test.stmt_use = usage_queue;

    int max_tid = -1;
    for (auto tid:tid_queue) {
        if (tid > max_tid)
            max_tid = tid;
    }

    re_test.trans_num = max_tid + 1;
    delete[] re_test.trans_arr;
    re_test.trans_arr = new transaction[re_test.trans_num];

    cerr << "txn num: " << re_test.trans_num << endl
        << "tid_queue size: " << re_test.tid_queue.size() << endl
        << "stmt_queue size: " << re_test.stmt_queue.size() << endl;
    if (re_test.tid_queue.size() != re_test.stmt_queue.size()) {
        cerr << "tid queue size should equal to stmt queue size" << endl;
        return;
    }

    // init each transaction stmt
    for (int i = 0; i < re_test.stmt_num; i++) {
        auto tid = re_test.tid_queue[i];
        re_test.trans_arr[tid].stmts.push_back(re_test.stmt_queue[i]);
    }

    for (int tid = 0; tid < re_test.trans_num; tid++) {
        re_test.trans_arr[tid].dut = dut_setup(d_info);
        re_test.trans_arr[tid].stmt_num = re_test.trans_arr[tid].stmts.size();
        if (re_test.trans_arr[tid].stmts.empty()) {
            re_test.trans_arr[tid].status = TXN_ABORT;
            continue;
        }

        auto stmt_str = print_stmt_to_string(re_test.trans_arr[tid].stmts.back());
        if (stmt_str.find("COMMIT") != string::npos)
            re_test.trans_arr[tid].status = TXN_COMMIT;
        else
            re_test.trans_arr[tid].status = TXN_ABORT;
    }

    try {
        re_test.trans_test();
        shared_ptr<dependency_analyzer> tmp_da;
        re_test.analyze_txn_dependency(tmp_da);
        set<int> cycle_nodes;
        vector<int> sorted_nodes;
        tmp_da->check_txn_graph_cycle(cycle_nodes, sorted_nodes);
        tmp_da->print_dependency_graph();
        if (!cycle_nodes.empty()) { // need decycle
            for (auto txn_id : cycle_nodes) {
                auto new_stmt_queue = stmt_queue;
                auto new_usage_queue = usage_queue;
                int stmt_num = new_stmt_queue.size();

                // delete the txn whose id is txn_id
                for (int i = 0; i < stmt_num; i++) {
                    if (tid_queue[i] != txn_id)
                        continue;
                    // commit and abort stmt
                    if (usage_queue[i] == INIT_TYPE)
                        continue;
                    new_stmt_queue[i] = make_shared<txn_string_stmt>((prod *)0, SPACE_HOLDER_STMT);
                    new_usage_queue[i] = INIT_TYPE;
                    new_usage_queue[i].is_instrumented = false;
                }

                for (int tid = 0; tid < re_test.trans_num; tid++) {
                    re_test.trans_arr[tid].dut.reset();
                }

                delete_nodes.push_back(txn_id);
                cerr << "delete nodes: ";
                for (auto node:delete_nodes)
                    cerr << node << " ";
                cerr << endl;

                // after deleting the txn, try it again
                txn_decycle_test(d_info, new_stmt_queue, tid_queue, new_usage_queue, succeed_time, all_time, delete_nodes);
                delete_nodes.pop_back();
            }
        }
        else { // no cycle, perform txn sorting and check results
            vector<stmt_id> txn_stmt_path;
            for (auto txn_id:sorted_nodes) {
                auto txn_stmt_num = re_test.trans_arr[txn_id].stmt_num;
                for (int count = 0; count < txn_stmt_num; count++) {
                    auto s_id = stmt_id(txn_id, count);
                    auto stmt_idx = s_id.transfer_2_stmt_idx(tid_queue);
                    if (usage_queue[stmt_idx] == INIT_TYPE) // skip begin, commit, abort, SPACE_HOLDER_STMT
                        continue;
                    txn_stmt_path.push_back(s_id);
                }
            }

            re_test.normal_stmt_test(txn_stmt_path);
            if (re_test.check_normal_stmt_result(txn_stmt_path, false) == false) {
                string bug_str = "Find bugs in check_normal_stmt_result";
                cerr << RED << bug_str << RESET << endl;
                succeed_time++;
            }
            all_time++;
            cerr << "succeed_time: " << succeed_time << " all_time: " << all_time << endl;
        }
    } catch (exception &e) {
        string cur_err_info = e.what();
        cerr << "exception captured by test: " << cur_err_info << endl;
        all_time++;
        cerr << "succeed_time: " << succeed_time << " all_time: " << all_time << endl;
    }

    return;
}

void check_topo_sort(dbms_info& d_info,
                    vector<shared_ptr<prod>>& stmt_queue,
                    vector<int>& tid_queue,
                    vector<stmt_usage>& usage_queue,
                    int& succeed_time,
                    int& all_time)
{
    transaction_test::fork_if_server_closed(d_info);

    transaction_test re_test(d_info);
    re_test.stmt_queue = stmt_queue;
    re_test.tid_queue = tid_queue;
    re_test.stmt_num = re_test.tid_queue.size();
    re_test.stmt_use = usage_queue;

    int max_tid = -1;
    for (auto tid:tid_queue) {
        if (tid > max_tid)
            max_tid = tid;
    }

    re_test.trans_num = max_tid + 1;
    delete[] re_test.trans_arr;
    re_test.trans_arr = new transaction[re_test.trans_num];

    cerr << "txn num: " << re_test.trans_num << endl
        << "tid_queue size: " << re_test.tid_queue.size() << endl
        << "stmt_queue size: " << re_test.stmt_queue.size() << endl;
    if (re_test.tid_queue.size() != re_test.stmt_queue.size()) {
        cerr << "tid queue size should equal to stmt queue size" << endl;
        return;
    }

    // init each transaction stmt
    for (int i = 0; i < re_test.stmt_num; i++) {
        auto tid = re_test.tid_queue[i];
        re_test.trans_arr[tid].stmts.push_back(re_test.stmt_queue[i]);
    }

    for (int tid = 0; tid < re_test.trans_num; tid++) {
        re_test.trans_arr[tid].dut = dut_setup(d_info);
        re_test.trans_arr[tid].stmt_num = re_test.trans_arr[tid].stmts.size();
        if (re_test.trans_arr[tid].stmts.empty()) {
            re_test.trans_arr[tid].status = TXN_ABORT;
            continue;
        }

        auto stmt_str = print_stmt_to_string(re_test.trans_arr[tid].stmts.back());
        if (stmt_str.find("COMMIT") != string::npos)
            re_test.trans_arr[tid].status = TXN_COMMIT;
        else
            re_test.trans_arr[tid].status = TXN_ABORT;
    }

    try {
        re_test.trans_test();
        shared_ptr<dependency_analyzer> tmp_da;
        re_test.analyze_txn_dependency(tmp_da);
        auto all_topo_sort = tmp_da->get_all_topo_sort_path();
	cerr << "topo sort size: " << all_topo_sort.size() << endl;
        for (auto& sort : all_topo_sort) {
            cerr << RED << "stmt path for normal test: " << RESET;
            print_stmt_path(sort, tmp_da->stmt_dependency_graph);

            re_test.normal_stmt_output.clear();
            re_test.normal_stmt_err_info.clear();
            re_test.normal_stmt_db_content.clear();
            re_test.normal_stmt_test(sort);
            if (re_test.check_normal_stmt_result(sort, false) == false) {
                succeed_time++;
            }
            all_time++;
            cerr << "succeed_time: " << succeed_time << " all_time: " << all_time << "/" << all_topo_sort.size() << endl;
        }
    } catch (exception &e) {
        string cur_err_info = e.what();
        cerr << "exception captured by test: " << cur_err_info << endl;
        all_time++;
        cerr << "succeed_time: " << succeed_time << " all_time: " << all_time << endl;
    }

    return;
}

// kill_process_with_SIGTERM is defined in transaction_test.cc
// mutex_timeout/cond_timeout are defined in txcheck_run.cc