/// @file
/// @brief Base class providing schema information to grammar

#ifndef SCHEMA_HH
#define SCHEMA_HH

#include <string>
#include <iostream>
#include <pqxx/pqxx>
#include <numeric>
#include <memory>

#include "core/relmodel.hh"
#include "core/random.hh"
#include "core/prod.hh"

#define BINOP(n, a, b, r) do {\
    op o(#n, a, b, r); \
    register_operator(o); \
} while(0)

#define FUNC(n, r) do {							\
    routine proc("", "", r, #n);				\
    register_routine(proc);						\
} while(0)

#define FUNC1(n, r, a) do {						\
    routine proc("", "", r, #n);				\
    proc.argtypes.push_back(a);				\
    register_routine(proc);						\
} while(0)

#define FUNC2(n, r, a, b) do {						\
    routine proc("", "", r, #n);				\
    proc.argtypes.push_back(a);				\
    proc.argtypes.push_back(b);				\
    register_routine(proc);						\
} while(0)

#define FUNC3(n, r, a, b, c) do {						\
    routine proc("", "", r, #n);				\
    proc.argtypes.push_back(a);				\
    proc.argtypes.push_back(b);				\
    proc.argtypes.push_back(c);				\
    register_routine(proc);						\
} while(0)

#define FUNC4(n, r, a, b, c, d) do {						\
    routine proc("", "", r, #n);				\
    proc.argtypes.push_back(a);				\
    proc.argtypes.push_back(b);				\
    proc.argtypes.push_back(c);				\
    proc.argtypes.push_back(d);				\
    register_routine(proc);						\
} while(0)

#define FUNC5(n, r, a, b, c, d, e) do {						\
    routine proc("", "", r, #n);				\
    proc.argtypes.push_back(a);				\
    proc.argtypes.push_back(b);				\
    proc.argtypes.push_back(c);				\
    proc.argtypes.push_back(d);				\
    proc.argtypes.push_back(e);				\
    register_routine(proc);						\
} while(0)

#define AGG1(n, r, a) do {						\
    routine proc("", "", r, #n);				\
    proc.argtypes.push_back(a);				\
    register_aggregate(proc);						\
} while(0)

#define AGG2(n, r, a, b) do {						\
    routine proc("", "", r, #n);				\
    proc.argtypes.push_back(a);				\
    proc.argtypes.push_back(b);				\
    register_aggregate(proc);						\
} while(0)

#define AGG3(n, r, a, b, c) do {						\
    routine proc("", "", r, #n);				\
    proc.argtypes.push_back(a);				\
    proc.argtypes.push_back(b);				\
    proc.argtypes.push_back(c);				\
    register_aggregate(proc);						\
} while(0)

#define AGG(n, r) do {						\
    routine proc("", "", r, #n);				\
    register_aggregate(proc);						\
} while(0)

#define WIN(n, r) do {						\
    routine proc("", "", r, #n);				\
    register_windows(proc);						\
} while(0)

#define WIN1(n, r, a) do {						\
    routine proc("", "", r, #n);				\
    proc.argtypes.push_back(a);				\
    register_windows(proc);						\
} while(0)

#define WIN2(n, r, a, b) do {						\
    routine proc("", "", r, #n);				\
    proc.argtypes.push_back(a);				\
    proc.argtypes.push_back(b);				\
    register_windows(proc);						\
} while(0)

struct schema {
    sqltype *booltype = NULL;
    sqltype *inttype = NULL;
    sqltype *realtype = NULL;
    sqltype *texttype = NULL;
    sqltype *internaltype = NULL;
    sqltype *arraytype = NULL;
    sqltype *datetype = NULL;
    sqltype *enumtype = NULL;          // ENUM type for MySQL/GaussDB

    std::vector<sqltype *> types;
  
    std::vector<table> tables;
    std::vector<string> indexes;
    std::vector<op> operators;
    std::vector<routine> routines;
    std::vector<routine> aggregates;
    std::vector<routine> windows;

    typedef std::tuple<sqltype *,sqltype *,sqltype *> typekey;
    std::multimap<typekey, op> index;
    typedef std::multimap<typekey, op>::iterator op_iterator;

    std::multimap<sqltype*, routine*> routines_returning_type;
    std::multimap<sqltype*, routine*> aggregates_returning_type;
    std::multimap<sqltype*, routine*> windows_returning_type;
    std::multimap<sqltype*, routine*> parameterless_routines_returning_type;
    std::multimap<sqltype*, table*> tables_with_columns_of_type;
    std::multimap<sqltype*, op*> operators_returning_type;
    std::multimap<sqltype*, sqltype*> concrete_type;
    std::vector<table*> base_tables;

    string version;
    int version_num; // comparable version number

    const char *true_literal = "true";
    const char *false_literal = "false";
    const char *null_literal = "null";

    vector<string> available_collation;
    vector<string> available_enum_defs;   // Pre-defined ENUM definitions e.g. "ENUM('a','b','c')"
    bool enable_partial_index = false; // can or cannot use where in indexes
    vector<string> available_table_options;
    bool enable_analyze_stmt = false; //  can or cannot use analyze statement
    vector<string> compound_operators;
    vector<string> available_index_type;
    vector<string> available_index_keytype;
    static string target_dbms;
    vector<string> supported_join_op;
    vector<string> supported_table_engine;
    map<string, vector<string>> supported_setting;
    static bool require_pkey_wkey;

    /// Feature flags for DBMS-specific SQL generation control
    struct db_feature_flags {
        // PostgreSQL features
        bool has_window_frame    = false;
        bool has_data_mod_cte    = false;
        bool has_quantified_cmp  = false;
        bool has_grouping_sets   = false;
        bool has_json_jsonb      = false;
        bool has_array_ops       = false;
        bool has_merge           = false;
        bool has_upsert          = false;
        bool has_returning       = false;
        bool has_tablesample     = false;
        bool has_lateral         = false;
        bool has_for_update      = false;

        // MySQL features
        bool has_on_duplicate_key = false;
        bool has_if_function      = false;
        bool has_group_concat     = false;

        // Common
        bool has_full_outer_join  = false;
        bool has_intersect_except = false;

        // MySQL-specific
        bool has_regexp           = false;
        bool has_sounds_like      = false;
        bool has_straight_join    = false;
        bool has_index_hints      = false;
        bool has_with_rollup      = false;
        bool has_replace          = false;
        bool has_do_stmt          = false;
        bool has_explain          = false;
        bool has_select_options   = false;

        // Common expressions
        bool has_cast             = false;   // CAST / CONVERT
        bool has_interval_expr    = false;   // date + INTERVAL n DAY
        bool has_mysql_json       = false;   // MySQL JSON ->/->>
        bool has_savepoint        = false;   // SAVEPOINT / RELEASE / ROLLBACK TO

        // Partition table support
        bool has_partition_table    = false;  // CREATE TABLE ... PARTITION BY
        bool has_subpartition       = false;  // SUBPARTITION BY HASH/KEY
        bool has_partition_mgmt     = false;  // ALTER TABLE ADD/DROP/TRUNCATE PARTITION
        bool has_partition_select   = false;  // SELECT ... PARTITION (p0)
        bool has_partition_default  = false;  // DEFAULT partition (PG)
        bool has_attach_partition   = false;  // ATTACH/DETACH PARTITION (PG)

        // MySQL 8.0 P0 features
        bool has_check_constraint   = false;  // CHECK (expr) in CREATE TABLE
        bool has_generated_column   = false;  // col AS (expr) STORED/VIRTUAL
        bool has_enum_type          = false;  // ENUM('val1','val2',...)
        bool has_xa_transaction     = false;  // XA START/END/PREPARE/COMMIT/ROLLBACK
        bool has_set_isolation      = false;  // SET TRANSACTION ISOLATION LEVEL
        bool has_correlated_subq    = false;  // correlated subqueries in WHERE
        bool has_multi_table_update = false;  // UPDATE t1, t2 SET ...
        bool has_multi_table_delete = false;  // DELETE t1 FROM t1 JOIN t2
        bool has_json_table         = false;  // JSON_TABLE() table function
    };
    db_feature_flags features;

    static string get_version_key_name() {
        return require_pkey_wkey ? WKEY_IDENT : VKEY_IDENT;
    }

    virtual std::string quote_name(const std::string &id) = 0;
  
    void summary() {
        std::cout << "Found " << tables.size() <<
            " user table(s) in information schema." << std::endl;
    }

    void fill_scope(struct scope &s) {
        for (auto &t : tables)
            s.tables.push_back(&t);
        for (auto i : indexes)
            s.indexes.push_back(i);
        s.schema = this;
    }

    virtual void register_operator(op& o) {
        operators.push_back(o);
        typekey t(o.left, o.right, o.result);
        index.insert(std::pair<typekey, op>(t, o));
    }

    virtual void register_routine(routine& r) {
        routines.push_back(r);
    }

    virtual void register_aggregate(routine& r) {
        aggregates.push_back(r);
    }

    virtual void register_windows(routine& r) {
        windows.push_back(r);
    }

    virtual op_iterator find_operator(sqltype *left, sqltype *right, sqltype *res) {
        typekey t(left, right, res);
        auto cons = index.equal_range(t);
        if (cons.first == cons.second)
            return index.end();
        else
            return random_pick<>(cons.first, cons.second);
    }

    schema() { }
    // virtual void update_schema() = 0; // only update dynamic information, e.g. table, columns, index
    void generate_indexes();
};

#endif

