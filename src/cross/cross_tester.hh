/// @file
/// @brief Cross-mode tester: combines TxCheck transaction testing with EET equivalent transformation

#ifndef CROSS_TESTER_HH
#define CROSS_TESTER_HH

#include "core/dbms_info.hh"
#include "schema/schema.hh"
#include "schema/dut.hh"
#include "core/general_process.hh"
#include "txcheck/transaction_test.hh"
#include "txcheck/dependency_analyzer.hh"
#include <vector>
#include <memory>
#include <set>

using namespace std;

/// Returns true if the usage type represents a SELECT-like read
/// (SELECT_READ or VERSION_SET_READ from the instrumentor's stmt_basic_type enum)
bool is_select_like_usage(int usage_type);

struct cross_tester {
    dbms_info &d_info;
    shared_ptr<schema> db_schema;

    cross_tester(dbms_info &info, shared_ptr<schema> s);

    /// Main cross testing entry point.
    /// If bug_dir is non-empty, saves full bug report when a bug is found.
    /// Returns true if a bug is found, false otherwise.
    bool cross_test(const string& bug_dir = "");

    /// Minimize the cross-mode test case after bug confirmation.
    /// Performs statement-level reduction and SELECT transform reduction.
    void minimize(const string& bug_dir);

private:
    // Data from the last cross_test() run, used by minimize()
    vector<shared_ptr<prod>> last_path_stmts;
    vector<int> last_path_usages;
    bool last_bug_confirmed = false;

    /// Execute a sequence of statements without transaction wrapping.
    /// SELECT-like statements have their output rows collected into results.
    void execute_normal_path(
        vector<shared_ptr<prod>>& stmts,
        vector<int>& stmt_usages,
        multiset<row_output>& results,
        map<string, vector<vector<string>>>& final_content);

    /// Apply EET equivalent transform to a SELECT statement's bool_expr and value_expr nodes.
    /// Modifies the AST in place; returns the same shared_ptr.
    shared_ptr<prod> transform_select(shared_ptr<prod> stmt);

    /// Restore a previously transformed SELECT statement back to its original form.
    void back_transform_select(shared_ptr<prod> stmt);

    /// Re-validate a potential bug by re-executing both original and transformed paths.
    /// Returns true if the difference persists (bug confirmed), false if it was a false positive.
    bool revalidate_bug(
        vector<shared_ptr<prod>>& original_stmts,
        vector<int>& stmt_usages,
        multiset<row_output>& original_results,
        multiset<row_output>& transformed_results);

    /// Save bug-triggering statements and results to the given directory.
    void save_bug_report(
        const string& bug_dir,
        vector<shared_ptr<prod>>& path_stmts,
        vector<int>& path_usages,
        multiset<row_output>& tx_results,
        multiset<row_output>& normal_results,
        multiset<row_output>& transformed_results);

    /// Write a multiset of row_output to a file.
    static void write_results(const string& filename, multiset<row_output>& results);
};

#endif