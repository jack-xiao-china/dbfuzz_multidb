/// @file
/// @brief Cross-mode tester implementation
/// Combines TxCheck transaction testing with EET equivalent expression transformation.
///
/// Flow:
///   1. Run TxCheck to get dependency analysis and topological sort path
///   2. Extract non-instrumented data statements from the path
///   3. Execute: transaction path, normal path (sequential), transformed path (EET on SELECTs)
///   4. Three-way comparison with re-validation

#include "cross/cross_tester.hh"
#include "txcheck/tx_general_process.hh"
#include "expr/value_expr.hh"
#include "expr/bool_expr/bool_expr.hh"
#include "grammar/grammar.hh"

// ---------------------------------------------------------------------------
// is_select_like_usage
// ---------------------------------------------------------------------------
bool is_select_like_usage(int usage_type) {
    return usage_type == (int)SELECT_READ || usage_type == (int)VERSION_SET_READ;
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
cross_tester::cross_tester(dbms_info &info, shared_ptr<schema> s)
    : d_info(info), db_schema(s) {}

// ---------------------------------------------------------------------------
// execute_normal_path: run statements sequentially without transaction wrapping
// ---------------------------------------------------------------------------
void cross_tester::execute_normal_path(
    vector<shared_ptr<prod>>& stmts,
    vector<int>& stmt_usages,
    multiset<row_output>& results,
    map<string, vector<vector<string>>>& final_content)
{
    auto dut = dut_setup(d_info);
    for (size_t i = 0; i < stmts.size(); i++) {
        auto stmt_str = print_stmt_to_string(stmts[i]);
        stmt_output output;
        try {
            dut->test(stmt_str, &output);
            // Collect output rows for SELECT-like statements
            if (is_select_like_usage(stmt_usages[i])) {
                for (auto& row : output)
                    results.insert(row);
            }
        } catch (exception& e) {
            cerr << "execute_normal_path: stmt " << i << " failed: " << e.what() << endl;
        }
    }
    dut_get_content(d_info, final_content);
}

// ---------------------------------------------------------------------------
// transform_select: apply EET equivalent_transform to a SELECT's expressions
// ---------------------------------------------------------------------------
shared_ptr<prod> cross_tester::transform_select(shared_ptr<prod> stmt) {
    // Handle plain SELECT (query_spec)
    if (auto qs = dynamic_pointer_cast<query_spec>(stmt)) {
        // Transform WHERE clause
        qs->search->equivalent_transform();
        // Transform SELECT list value expressions
        for (auto& ve : qs->select_list->value_exprs)
            ve->equivalent_transform();
        // Transform HAVING clause if present
        if (qs->has_group)
            qs->group_clause->having_cond_search->equivalent_transform();
        return stmt;
    }
    // Handle CTE (WITH ... SELECT)
    if (auto cte = dynamic_pointer_cast<common_table_expression>(stmt)) {
        // Transform main query
        cte->query->search->equivalent_transform();
        for (auto& ve : cte->query->select_list->value_exprs)
            ve->equivalent_transform();
        if (cte->query->has_group)
            cte->query->group_clause->having_cond_search->equivalent_transform();
        // Transform WITH sub-queries
        for (auto& wq : cte->with_queries) {
            wq->search->equivalent_transform();
            for (auto& ve : wq->select_list->value_exprs)
                ve->equivalent_transform();
            if (wq->has_group)
                wq->group_clause->having_cond_search->equivalent_transform();
        }
        return stmt;
    }
    // Not a SELECT — return unchanged
    return stmt;
}

// ---------------------------------------------------------------------------
// back_transform_select: restore AST after equivalent_transform
// ---------------------------------------------------------------------------
void cross_tester::back_transform_select(shared_ptr<prod> stmt) {
    if (auto qs = dynamic_pointer_cast<query_spec>(stmt)) {
        qs->search->back_transform();
        for (auto& ve : qs->select_list->value_exprs)
            ve->back_transform();
        if (qs->has_group)
            qs->group_clause->having_cond_search->back_transform();
        return;
    }
    if (auto cte = dynamic_pointer_cast<common_table_expression>(stmt)) {
        cte->query->search->back_transform();
        for (auto& ve : cte->query->select_list->value_exprs)
            ve->back_transform();
        if (cte->query->has_group)
            cte->query->group_clause->having_cond_search->back_transform();
        for (auto& wq : cte->with_queries) {
            wq->search->back_transform();
            for (auto& ve : wq->select_list->value_exprs)
                ve->back_transform();
            if (wq->has_group)
                wq->group_clause->having_cond_search->back_transform();
        }
        return;
    }
}

// ---------------------------------------------------------------------------
// revalidate_bug: re-execute both paths to confirm the difference persists
// ---------------------------------------------------------------------------
bool cross_tester::revalidate_bug(
    vector<shared_ptr<prod>>& original_stmts,
    vector<int>& stmt_usages,
    multiset<row_output>& original_results,
    multiset<row_output>& transformed_results)
{
    // Re-execute original (normal) path — AST is in original state
    dut_reset_to_backup(d_info);
    multiset<row_output> re_normal;
    map<string, vector<vector<string>>> re_normal_content;
    execute_normal_path(original_stmts, stmt_usages, re_normal, re_normal_content);

    // Transform and re-execute
    vector<int> select_indices;
    for (size_t i = 0; i < original_stmts.size(); i++) {
        if (is_select_like_usage(stmt_usages[i])) {
            transform_select(original_stmts[i]);
            select_indices.push_back(i);
        }
    }
    dut_reset_to_backup(d_info);
    multiset<row_output> re_transformed;
    map<string, vector<vector<string>>> re_transformed_content;
    execute_normal_path(original_stmts, stmt_usages, re_transformed, re_transformed_content);

    // Back-transform to restore AST
    for (auto idx : select_indices)
        back_transform_select(original_stmts[idx]);

    // Check if the EET difference persists
    bool eet_diff_persists = (re_normal != re_transformed);
    bool state_diff_persists = !compare_content(re_normal_content, re_transformed_content);

    return eet_diff_persists || state_diff_persists;
}

// ---------------------------------------------------------------------------
// cross_test: main entry point
// ---------------------------------------------------------------------------
bool cross_tester::cross_test() {
    // -----------------------------------------------------------------------
    // Phase 1: Set up and run TxCheck to get dependency analysis
    // -----------------------------------------------------------------------
    transaction_test tt(d_info);

    try {
        tt.assign_txn_id();
        tt.assign_txn_status();
        tt.gen_txn_stmts();
    } catch (exception& e) {
        cerr << "cross_test: setup failed: " << e.what() << endl;
        return false;
    }

    // Block scheduling stabilizes the statement queue (replaces blocked stmts)
    tt.block_scheduling();
    // Instrument adds dependency-tracking statements
    tt.instrument_txn_stmts();

    // -----------------------------------------------------------------------
    // Phase 2: Execute the transaction and collect results
    // -----------------------------------------------------------------------
    // dut_backup is called before first execution; trans_test resets to backup internally
    dut_backup(d_info);

    try {
        tt.trans_test(false);
    } catch (exception& e) {
        cerr << "cross_test: trans_test failed: " << e.what() << endl;
        return false;
    }

    // -----------------------------------------------------------------------
    // Phase 3: Analyze dependency and get topological sort path
    // -----------------------------------------------------------------------
    shared_ptr<dependency_analyzer> da;
    try {
        if (tt.analyze_txn_dependency(da)) {
            cerr << "cross_test: TxCheck found anomaly during dependency analysis" << endl;
            return true;
        }
    } catch (exception& e) {
        cerr << "cross_test: analyze_txn_dependency failed: " << e.what() << endl;
        return false;
    }

    set<stmt_id> deleted_nodes;
    auto stmt_path = da->topological_sort_path(deleted_nodes);
    if (stmt_path.empty()) {
        cerr << "cross_test: topological sort path is empty" << endl;
        return false;
    }

    // -----------------------------------------------------------------------
    // Phase 4: Extract non-instrumented data statements from the path
    // -----------------------------------------------------------------------
    // Build a mapping: (txn_id, stmt_idx_in_txn) → stmt_queue index
    // so we can look up stmt_use for each statement in the topo sort path.
    map<pair<int,int>, int> tid_stmtpos_to_queue_idx;
    int stmt_pos_of_txn[tt.trans_num];
    for (int i = 0; i < tt.trans_num; i++)
        stmt_pos_of_txn[i] = 0;
    for (int i = 0; i < tt.stmt_num; i++) {
        auto tid = tt.tid_queue[i];
        tid_stmtpos_to_queue_idx[make_pair(tid, stmt_pos_of_txn[tid])] = i;
        stmt_pos_of_txn[tid]++;
    }

    vector<shared_ptr<prod>> path_stmts;
    vector<int> path_usages;
    multiset<row_output> tx_results;

    for (auto& sid : stmt_path) {
        auto tid = sid.txn_id;
        auto stmt_pos = sid.stmt_idx_in_txn;
        auto key = make_pair(tid, stmt_pos);

        if (tid_stmtpos_to_queue_idx.count(key) == 0)
            continue;
        auto queue_idx = tid_stmtpos_to_queue_idx[key];

        // Skip instrumented statements (dependency tracking reads)
        if (tt.stmt_use[queue_idx].is_instrumented)
            continue;

        // Skip control statements (BEGIN, COMMIT, ABORT, SPACE_HOLDER)
        auto casted = dynamic_pointer_cast<txn_string_stmt>(tt.stmt_queue[queue_idx]);
        if (casted && tt.stmt_use[queue_idx] == INIT_TYPE)
            continue;

        path_stmts.push_back(tt.trans_arr[tid].stmts[stmt_pos]);
        path_usages.push_back((int)tt.stmt_use[queue_idx].stmt_type);

        // Collect transaction output for SELECT-like statements
        if (is_select_like_usage(path_usages.back())) {
            if (stmt_pos < (int)tt.trans_arr[tid].stmt_outputs.size()) {
                auto& output = tt.trans_arr[tid].stmt_outputs[stmt_pos];
                for (auto& row : output)
                    tx_results.insert(row);
            }
        }
    }

    if (path_stmts.empty()) {
        cerr << "cross_test: no data statements in topological path" << endl;
        return false;
    }

    cerr << "cross_test: extracted " << path_stmts.size() << " data statements from path" << endl;
    auto& tx_content = tt.trans_db_content;

    // -----------------------------------------------------------------------
    // Phase 5: Execute normal path (sequential, non-transactional)
    // -----------------------------------------------------------------------
    dut_reset_to_backup(d_info);
    multiset<row_output> normal_results;
    map<string, vector<vector<string>>> normal_content;
    execute_normal_path(path_stmts, path_usages, normal_results, normal_content);

    // -----------------------------------------------------------------------
    // Phase 6: Build and execute transformed path (EET on SELECT statements)
    // -----------------------------------------------------------------------
    vector<int> select_indices;
    for (size_t i = 0; i < path_stmts.size(); i++) {
        if (is_select_like_usage(path_usages[i])) {
            transform_select(path_stmts[i]);
            select_indices.push_back(i);
        }
    }

    dut_reset_to_backup(d_info);
    multiset<row_output> transformed_results;
    map<string, vector<vector<string>>> transformed_content;
    execute_normal_path(path_stmts, path_usages, transformed_results, transformed_content);

    // Restore AST to original state
    for (auto idx : select_indices)
        back_transform_select(path_stmts[idx]);

    // -----------------------------------------------------------------------
    // Phase 7: Three-way comparison
    // -----------------------------------------------------------------------
    // Oracle 1: tx_results vs normal_results (TxCheck — does interleaving matter?)
    bool txcheck_output_ok = (tx_results == normal_results);
    // Oracle 2: tx_content vs normal_content (state consistency)
    bool txcheck_state_ok = compare_content(tx_content, normal_content);
    // Oracle 3: normal_results vs transformed_results (EET — does transform change output?)
    bool eet_output_ok = (normal_results == transformed_results);
    // Oracle 4: normal_content vs transformed_content (transform state safety)
    bool eet_state_ok = compare_content(normal_content, transformed_content);

    bool no_bug = txcheck_output_ok && txcheck_state_ok && eet_output_ok && eet_state_ok;

    if (no_bug) {
        cerr << "cross_test: all oracles passed, no bug" << endl;
        return false;
    }

    // Report which oracles failed
    cerr << YELLOW << "cross_test: difference detected!" << RESET << endl;
    if (!txcheck_output_ok)
        cerr << "  [TxCheck output] tx_results != normal_results" << endl;
    if (!txcheck_state_ok)
        cerr << "  [TxCheck state]  tx_content != normal_content" << endl;
    if (!eet_output_ok)
        cerr << "  [EET output]     normal_results != transformed_results" << endl;
    if (!eet_state_ok)
        cerr << "  [EET state]      normal_content != transformed_content" << endl;

    // -----------------------------------------------------------------------
    // Phase 8: Re-validation — re-execute both paths to confirm
    // -----------------------------------------------------------------------
    cerr << "cross_test: re-validating ..." << endl;
    bool bug_confirmed = revalidate_bug(
        path_stmts, path_usages, normal_results, transformed_results);

    if (!bug_confirmed) {
        cerr << "cross_test: difference did NOT persist (false positive)" << endl;
        return false;
    }

    cerr << RED << "cross_test: BUG CONFIRMED after re-validation!" << RESET << endl;
    return true;
}
