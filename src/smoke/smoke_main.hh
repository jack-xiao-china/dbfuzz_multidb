#ifndef SMOKE_MAIN_HH
#define SMOKE_MAIN_HH

#include "config.h"
#include <string>
#include <map>
#include "core/dbms_info.hh"

void smoke_run(dbms_info &d_info, std::map<std::string, std::string> &options);

#endif
