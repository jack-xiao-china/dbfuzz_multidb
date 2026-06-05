/// @file
/// @brief EET mode entry point and helper declarations

#ifndef EET_MAIN_HH
#define EET_MAIN_HH

#include "core/dbms_info.hh"
#include <map>
#include <string>
#include <set>
#include <vector>
#include <memory>
#include <fstream>
#include <iostream>

using namespace std;

// Forward declaration - qcn_tester is defined in eet/qcn_tester/qcn_tester.hh
struct qcn_tester;

// Global variables for tracking test progress
extern unsigned long long test_start_timestamp_ms;
extern unsigned long long dbms_execution_ms;
extern int executed_test_num;

// EET mode entry point
void eet_run(dbms_info &d_info, map<string, string> &options);

// Helper functions
void print_test_time_info();
void print_output_to_file(multiset<vector<string>> &output, string filename);
void minimize_qcn_database(shared_ptr<qcn_tester> qcn,
                            string input_db_record_file,
                            string output_db_record_file,
                            multiset<vector<string>>* min_origin_output,
                            multiset<vector<string>>* min_qit_output);

#endif
