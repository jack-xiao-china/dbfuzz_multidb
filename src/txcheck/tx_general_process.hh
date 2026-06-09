#ifndef TX_GENERAL_PROCESS_HH
#define TX_GENERAL_PROCESS_HH

/// @file
/// @brief TxCheck-specific process declarations.
/// Extends core/general_process.hh with txcheck-specific types and functions.

#include "core/general_process.hh"
#include "txcheck/transaction_test.hh"  // for transaction, stmt_usage, stmt_id
#include "txcheck/instrumentor.hh"       // for stmt_usage, stmt_id
#include "txcheck/dependency_analyzer.hh" // for dependency_analyzer

// TxCheck-specific overloaded functions (different signatures from core)
void interect_test(dbms_info& d_info,
                    shared_ptr<prod> (* tmp_statement_factory)(scope *),
                    vector<string>& rec_vec,
                    bool need_affect);

void normal_test(dbms_info& d_info,
                    shared_ptr<schema>& schema,
                    shared_ptr<prod> (* tmp_statement_factory)(scope *),
                    vector<string>& rec_vec,
                    bool need_affect);

int generate_database(dbms_info& d_info);  // 1-arg overload (core has 2-arg)

// TxCheck-specific unique functions
int use_backup_file(string backup_file, dbms_info& d_info);

bool reproduce_routine(dbms_info& d_info,
                        vector<shared_ptr<prod>>& stmt_queue,
                        vector<int>& tid_queue,
                        vector<stmt_usage> usage_queue,
                        string& err_info);

bool check_txn_cycle(dbms_info& d_info,
                        vector<shared_ptr<prod>>& stmt_queue,
                        vector<int>& tid_queue,
                        vector<stmt_usage>& usage_queue);

void txn_decycle_test(dbms_info& d_info,
                    vector<shared_ptr<prod>>& stmt_queue,
                    vector<int>& tid_queue,
                    vector<stmt_usage>& usage_queue,
                    int& succeed_time,
                    int& all_time,
                    vector<int> delete_nodes);

void check_topo_sort(dbms_info& d_info,
                    vector<shared_ptr<prod>>& stmt_queue,
                    vector<int>& tid_queue,
                    vector<stmt_usage>& usage_queue,
                    int& succeed_time,
                    int& all_time);

bool minimize_testcase(dbms_info& d_info,
                        vector<shared_ptr<prod>>& stmt_queue,
                        vector<int>& tid_queue,
                        vector<stmt_usage> usage_queue);

void save_current_testcase(vector<shared_ptr<prod>>& stmt_queue,
                            vector<int>& tid_queue,
                            vector<stmt_usage>& usage_queue,
                            string stmt_file_name,
                            string tid_file_name,
                            string usage_file_name);

void kill_process_with_SIGTERM(pid_t process_id);

extern pthread_mutex_t mutex_timeout;
extern pthread_cond_t  cond_timeout;

#endif