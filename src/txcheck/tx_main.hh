/// @file
/// @brief TxCheck mode stub - forwards to tx_main logic
/// @note The original transfuzz.cc main() will be refactored into txcheck_run() in Phase 2

#ifndef TX_MAIN_HH
#define TX_MAIN_HH

#include "core/dbms_info.hh"
#include <map>
#include <string>

using namespace std;

void txcheck_run(dbms_info &d_info, map<string, string> &options);

#endif
