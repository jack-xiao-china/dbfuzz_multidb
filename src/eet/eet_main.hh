/// @file
/// @brief EET mode stub - forwards to eet_main logic
/// @note The original qcn.cc main() will be refactored into eet_run() in Phase 1

#ifndef EET_MAIN_HH
#define EET_MAIN_HH

#include "core/dbms_info.hh"
#include <map>
#include <string>

using namespace std;

void eet_run(dbms_info &d_info, map<string, string> &options);

#endif
