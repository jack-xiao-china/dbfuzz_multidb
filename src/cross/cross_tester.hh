/// @file
/// @brief Cross-mode tester: combines TxCheck transaction testing with EET equivalent transformation

#ifndef CROSS_TESTER_HH
#define CROSS_TESTER_HH

#include "core/dbms_info.hh"
#include "schema/schema.hh"

// Placeholder - to be implemented in Phase 3
struct cross_tester {
    dbms_info &d_info;
    shared_ptr<schema> db_schema;

    cross_tester(dbms_info &info, shared_ptr<schema> s)
        : d_info(info), db_schema(s) {}

    void cross_test();
};

#endif
