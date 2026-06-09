#include "schema/gaussdb.hh"

#include <iostream>
#include <fstream>
#include <iomanip>
#include <unistd.h>
#include <cmath>

#ifndef HAVE_BOOST_REGEX
#include <regex>
#else
#include <boost/regex.hpp>
using boost::regex;
using boost::smatch;
using boost::regex_match;
#endif

using namespace std;

#define GAUSSDB_TIMEOUT_SECOND 6

#define GAUSSDB_BK_FILE(DB_NAME) ("/tmp/gaussdb_" + DB_NAME + "_bk.sql")

static regex e_timeout("ERROR:  canceling statement due to statement timeout(\n|.)*");
static regex e_syntax("ERROR:  syntax error at or near(\n|.)*");

#define debug_info (string(__func__) + "(" + string(__FILE__) + ":" + to_string(__LINE__) + ")")

static bool has_types = false;
static vector<gaussdb_type *> static_type_vec;

static bool has_operators = false;
static vector<op> static_op_vec;

static bool has_routines = false;
static vector<routine> static_routine_vec;

static bool has_routine_para = false;
static map<string, vector<gaussdb_type *>> static_routine_para_map;

static bool has_aggregates = false;
static vector<routine> static_aggregate_vec;

static bool has_aggregate_para = false;
static map<string, vector<gaussdb_type *>> static_aggregate_para_map;

static vector<string> gaussdberrmsg;

static bool is_double(string myString, long double& result) {
    istringstream iss(myString);
    iss >> noskipws >> result; // noskipws considers leading whitespace invalid
    // Check the entire string was consumed and if either failbit or badbit is set
    return iss.eof() && !iss.fail();
}

static string process_number_string(string str)
{
    str.erase(0, str.find_first_not_of(" "));
    str.erase(str.find_last_not_of(" ") + 1);

    // process the string if the string is a number
    string final_str;
    long double result;
    if (is_double(str, result) == false) {
        if (str == "-Infinity")
            str  = "Infinity";
        final_str = str;
    }
    else {
        if (result == 0) // result can be -0, represent it as 0
            final_str = "0";
        else {
            stringstream ss;
            int precision = 5;
            if (log10(result) > precision) // keep 5 valid number
                ss << setprecision(precision) << result;
            else // remove the number behind digit point
                ss << setiosflags(ios::fixed) << setprecision(0) << result;
            final_str = ss.str();
        }
    }
    return final_str;
}

bool gaussdb_type::consistent(sqltype *rvalue)
{
    gaussdb_type *t = dynamic_cast<gaussdb_type*>(rvalue);
    if (!t) {
        cerr << "unknown type: " << rvalue->name  << endl;
        return false;
    }

    switch(typtype_) {
        case 'b': /* base type */
        case 'c': /* composite type */
        case 'd': /* domain */
        case 'r': /* range */
        case 'e': /* enum */
        case 'm': /* multirange */
            return this == t;
        case 's': /* set pseudotype (GaussDB: anyset) */
            return false;
        case 'u': /* undefined (GaussDB) */
            return false;
        case 'p':
            if (name == "anyarray") {
                return t->typelem_ != InvalidOid;
            } else if (name == "anynonarray") {
                return t->typelem_ == InvalidOid;
            } else if (name == "anyelement") {
                return t->typelem_ == InvalidOid;
            } else if(name == "anyenum") {
                return t->typtype_ == 'e';
            } else if (name == "anyrange") {
                return t->typtype_ == 'r';
            } else if (name == "record") {
                return t->typtype_ == 'c';
            } else if (name == "cstring") {
                return this == t;
            } else if (name == "any") {
                return true;
            } else if (name == "void") {
                return this == t;
            } else {
                return false;
            }
        default:
            cerr << "error type: " << name << " " << oid_ << " " << typdelim_ << " "
                << typrelid_ << " " << typelem_ << " " << typarray_ << " " << typtype_ << endl;
            cerr << "t type: " << t->name << " " << t->oid_ << " " << t->typdelim_ << " "
                << t->typrelid_ << " " << t->typelem_ << " " << t->typarray_ << " " << t->typtype_ << endl;
            throw std::logic_error("unknown typtype");
    }
}

static PGresult* pqexec_handle_error(PGconn *conn, string& query)
{
    auto res = PQexec(conn, query.c_str());
    auto status = PQresultStatus(res);
    if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK){
        string err = PQerrorMessage(conn);
        PQclear(res);
        throw runtime_error(err + " in " + debug_info);
    }
    return res;
}

// true: the proc is suitable
static bool is_suitable_proc(string proc_name)
{
    if (proc_name.find("pg_") != string::npos)
        return false;

    if (proc_name == "clock_timestamp"
        || proc_name == "inet_client_port"
        || proc_name == "now"
        || proc_name.find("random") != string::npos
        || proc_name == "statement_timestamp"
        || proc_name == "timeofday"
        || (proc_name.find("has_") != string::npos && proc_name.find("_privilege") != string::npos)
        || proc_name == "set_config"
        || proc_name.find("current") != string::npos
        || proc_name == "row_security_active"
        || proc_name == "string_agg" // may generate random-ordered string
        || proc_name == "regr_slope" // may give undetermine result when the slope close to infinite or 0
        ) {
        return false;
    }

    return true;
}

bool schema_gaussdb::is_consistent_with_basic_type(sqltype *rvalue)
{
    if (booltype->consistent(rvalue) ||
        inttype->consistent(rvalue) ||
        realtype->consistent(rvalue) ||
        texttype->consistent(rvalue) ||
        datetype->consistent(rvalue))
        return true;

    return false;
}

schema_gaussdb::schema_gaussdb(string db, unsigned int port, string host, string user, string pass, bool no_catalog)
    : gaussdb_connection(db, port, host, user, pass)
{
    ifstream gaussdberr("gaussdberr.txt");
    if (gaussdberr.is_open()) {
        std::string line;
        while (gaussdberr >> line)
            gaussdberrmsg.push_back(line);
    } else {
        std::cerr << "Unable to open gaussdberr.txt for reading." << std::endl;
    }

    string version_sql = "select version();";
    auto res = pqexec_handle_error(conn, version_sql);
    version = PQgetvalue(res, 0, 0);
    PQclear(res);

    string version_num_sql = "SHOW server_version_num;";
    res = pqexec_handle_error(conn, version_num_sql);
    version_num = atoi(PQgetvalue(res, 0, 0));
    PQclear(res);

    // GaussDB uses prokind similar to PostgreSQL 11+
    string procedure_is_aggregate = "prokind = 'a'";
    string procedure_is_window = "prokind = 'w'";

    // Load types
    if (has_types == false) {
        string load_type_sql = "select typname, oid, typdelim, typrelid, typelem, typarray, typtype "
            "from pg_type ;";
        res = pqexec_handle_error(conn, load_type_sql);
        auto row_num = PQntuples(res);
        for (int i = 0; i < row_num; i++) {
            string name(PQgetvalue(res, i, 0));
            OID oid = atol(PQgetvalue(res, i, 1));
            string typdelim(PQgetvalue(res, i, 2));
            OID typrelid = atol(PQgetvalue(res, i, 3));
            OID typelem = atol(PQgetvalue(res, i, 4));
            OID typarray = atol(PQgetvalue(res, i, 5));
            string typtype(PQgetvalue(res, i, 6));

            auto t = new gaussdb_type(name, oid, typdelim[0], typrelid, typelem, typarray, typtype[0]);
            static_type_vec.push_back(t);
        }
        PQclear(res);
        has_types = true;
    }
    for (auto t : static_type_vec) {
        oid2type[t->oid_] = t;
        name2type[t->name] = t;
        types.push_back(t);
    }

    if (name2type.count("bool") > 0 &&
            name2type.count("int4") > 0 &&
            name2type.count("numeric") > 0 &&
            name2type.count("text") > 0 &&
            name2type.count("timestamp") > 0) {

        booltype = name2type["bool"];
        inttype = name2type["int4"];
        realtype = name2type["numeric"];
        texttype = name2type["text"];
        datetype = name2type["timestamp"];
    }
    else {
        cerr << "at least one of booltype, inttype, realtype, texttype is not exist in" << debug_info << endl;
        throw runtime_error("at least one of booltype, inttype, realtype, texttype is not exist in" + debug_info);
    }

    internaltype = name2type["internal"];
    arraytype = name2type["anyarray"];
    true_literal = "true";
    false_literal = "false";
    null_literal = "null";

    compound_operators.push_back("union");
    compound_operators.push_back("union all");
    compound_operators.push_back("intersect");
    compound_operators.push_back("intersect all");
    compound_operators.push_back("except");
    compound_operators.push_back("except all");

    supported_join_op.push_back("cross");
    supported_join_op.push_back("inner");
    supported_join_op.push_back("left outer");
    supported_join_op.push_back("right outer");
    supported_join_op.push_back("full outer");

    target_dbms = "gaussdb";

    // GaussDB internal schemas to filter out
    vector<string> internal_schemas = {
        "pg_catalog", "information_schema", "snapshot", "dbe_perf",
        "dbe_pldeveloper", "gaussdb", "pkg_service", "blockchain",
        "cstore", "db4ai", "model_warehouse", "oracle", "sqladvisor",
        "pkg_util", "dbe_sql_util"
    };

    // Load tables
    // Note: GaussDB doesn't have is_insertable_into column, so we use 'YES' as default
    string load_table_sql = "select table_name, "
                                "table_schema, "
                                "'YES' as is_insertable_into, "
                                "table_type "
                            "from information_schema.tables;";
    res = pqexec_handle_error(conn, load_table_sql);
    auto row_num = PQntuples(res);
    for (int i = 0; i < row_num; i++) {
        string table_name(PQgetvalue(res, i, 0));
        string schema(PQgetvalue(res, i, 1));
        string insertable(PQgetvalue(res, i, 2));
        string table_type(PQgetvalue(res, i, 3));

        if (no_catalog) {
            bool is_internal = false;
            for (const auto& s : internal_schemas) {
                if (schema == s) {
                    is_internal = true;
                    break;
                }
            }
            if (is_internal)
                continue;
        }

        tables.push_back(table(table_name, schema,
                ((insertable == "YES") ? true : false),
                ((table_type == "BASE TABLE") ? true : false)));
    }
    PQclear(res);

    // Load columns and constraints
    for (auto t = tables.begin(); t != tables.end(); ++t) {
        string q("select attname, atttypid "
                "from pg_attribute join pg_class c on( c.oid = attrelid ) "
                    "join pg_namespace n on n.oid = relnamespace "
                "where not attisdropped "
                    "and attname not in "
                    "('xmin', 'xmax', 'ctid', 'cmin', 'cmax', 'tableoid', 'oid') ");
        q += " and relname = '" + t->name + "'";
        q += " and nspname = '" + t->schema + "';";

        res = pqexec_handle_error(conn, q);
        row_num = PQntuples(res);
        for (int i = 0; i < row_num; i++) {
            string column_name(PQgetvalue(res, i, 0));
            auto column_type = oid2type[atol(PQgetvalue(res, i, 1))];
            column c(column_name, column_type);
            t->columns().push_back(c);
        }
        PQclear(res);

        q = "select conname from pg_class t "
                "join pg_constraint c on (t.oid = c.conrelid) "
                "where contype in ('f', 'u', 'p') ";
        q = q + " and relnamespace = (select oid from pg_namespace where nspname = '" + t->schema + "')";
        q = q + " and relname = '" + t->name + "';";

        res = pqexec_handle_error(conn, q);
        row_num = PQntuples(res);
        for (int i = 0; i < row_num; i++) {
            t->constraints.push_back(PQgetvalue(res, i, 0));
        }
        PQclear(res);
    }

    // Load operators
    if (has_operators == false) {
        string load_operators_sql = "select oprname, oprleft,"
                                    "oprright, oprresult "
                                "from pg_catalog.pg_operator "
                                "where 0 not in (oprresult, oprright, oprleft) ;";
        res = pqexec_handle_error(conn, load_operators_sql);
        row_num = PQntuples(res);
        for (int i = 0; i < row_num; i++) {
            string op_name(PQgetvalue(res, i, 0));
            auto op_left_type = oid2type[atol(PQgetvalue(res, i, 1))];
            auto op_right_type = oid2type[atol(PQgetvalue(res, i, 2))];
            auto op_result_type = oid2type[atol(PQgetvalue(res, i, 3))];

            // Skip if any type not found in catalog
            if (!op_left_type || !op_right_type || !op_result_type)
                continue;

            // only consider basic type
            if (!is_consistent_with_basic_type(op_left_type) ||
                !is_consistent_with_basic_type(op_right_type) ||
                !is_consistent_with_basic_type(op_result_type))
                continue;

            op o(op_name, op_left_type, op_right_type, op_result_type);

            if (op_name == "&<|") {
                continue;
            }
            static_op_vec.push_back(o);
        }
        PQclear(res);
        has_operators = true;
    }
    for (auto& o:static_op_vec) {
        register_operator(o);
    }

    // Load routines
    if (has_routines == false) {
        string load_routines_sql =
            "select (select nspname from pg_namespace where oid = pronamespace), oid, prorettype, proname "
            "from pg_proc "
            "where prorettype::regtype::text not in ('event_trigger', 'trigger', 'opaque', 'internal') "
                "and not (proretset or " + procedure_is_aggregate + " or " + procedure_is_window + ") ;";

        res = pqexec_handle_error(conn, load_routines_sql);
        row_num = PQntuples(res);
        for (int i = 0; i < row_num; i++) {
            string r_name(PQgetvalue(res, i, 0));
            string oid_str(PQgetvalue(res, i, 1));
            auto prorettype = oid2type[atol(PQgetvalue(res, i, 2))];
            string proname(PQgetvalue(res, i, 3));

            // Skip if return type not found in catalog
            if (!prorettype)
                continue;

            // only consider basic type
            if (!is_consistent_with_basic_type(prorettype))
                continue;

            if (!is_suitable_proc(proname))
                continue;

            routine proc(r_name, oid_str, prorettype, proname);
            static_routine_vec.push_back(proc);
        }
        PQclear(res);
        has_routines = true;
    }

    // Load routine parameters
    if (has_routine_para == false) {
        for (int i = 0; i < static_routine_vec.size(); i++) {
            auto& proc = static_routine_vec[i];
            string q("select unnest(proargtypes) from pg_proc ");
            q = q + " where oid = " + proc.specific_name + ";";

            res = pqexec_handle_error(conn, q);
            row_num = PQntuples(res);

            bool has_not_basic_type = false;
            vector <gaussdb_type *> para_vec;
            for (int i = 0; i < row_num; i++) {
                auto t = oid2type[atol(PQgetvalue(res, i, 0))];
                if (!t) {
                    has_not_basic_type = true;
                    break;
                }
                if (!is_consistent_with_basic_type(t)) {
                    has_not_basic_type = true;
                    break;
                }
                para_vec.push_back(t);
            }
            if (has_not_basic_type) {
                static_routine_vec.erase(static_routine_vec.begin() + i);
                i--;
                continue;
            }
            static_routine_para_map[proc.specific_name] = para_vec;
            PQclear(res);
        }
        has_routine_para = true;
    }

    for (auto& proc:static_routine_vec) {
        register_routine(proc);
    }

    for (auto &proc : routines) {
        auto& para_vec = static_routine_para_map[proc.specific_name];
        for (auto t:para_vec) {
            proc.argtypes.push_back(t);
        }
    }

    // Load aggregates
    if (has_aggregates == false) {
        string load_aggregates_sql =
            "select (select nspname from pg_namespace where oid = pronamespace), oid, prorettype, proname "
            "from pg_proc "
                "where prorettype::regtype::text not in ('event_trigger', 'trigger', 'opaque', 'internal') "
                "and proname not in ('pg_event_trigger_table_rewrite_reason') "
                "and proname not in ('percentile_cont', 'dense_rank', 'cume_dist', "
                "'rank', 'test_rank', 'percent_rank', 'percentile_disc', 'mode', 'test_percentile_disc') "
                "and proname !~ '^ri_fkey_' "
                "and not (proretset or " + procedure_is_window + ") "
                "and " + procedure_is_aggregate + ";";
        res = pqexec_handle_error(conn, load_aggregates_sql);
        row_num = PQntuples(res);
        for (int i = 0; i < row_num; i++) {
            string nspname(PQgetvalue(res, i, 0));
            string oid_str(PQgetvalue(res, i, 1));
            auto prorettype = oid2type[atol(PQgetvalue(res, i, 2))];
            string proname(PQgetvalue(res, i, 3));

            // Skip if return type not found in catalog
            if (!prorettype)
                continue;

            // only consider basic type
            if (!is_consistent_with_basic_type(prorettype))
                continue;

            if (!is_suitable_proc(proname))
                continue;

            routine proc(nspname, oid_str, prorettype, proname);
            static_aggregate_vec.push_back(proc);
        }
        PQclear(res);
        has_aggregates = true;
    }

    // Load aggregate parameters
    if (has_aggregate_para == false) {
        for (int i = 0; i < static_aggregate_vec.size(); i++) {
            auto& proc = static_aggregate_vec[i];
            string q("select unnest(proargtypes) "
                "from pg_proc ");
            q = q + " where oid = " + proc.specific_name + ";";
            res = pqexec_handle_error(conn, q);
            row_num = PQntuples(res);

            bool has_not_basic_type = false;
            vector<gaussdb_type *> para_vec;
            for (int i = 0; i < row_num; i++) {
                auto t = oid2type[atol(PQgetvalue(res, i, 0))];
                if (!t) {
                    has_not_basic_type = true;
                    break;
                }
                if (!is_consistent_with_basic_type(t)) {
                    has_not_basic_type = true;
                    break;
                }
                para_vec.push_back(t);
            }
            if (has_not_basic_type) {
                static_aggregate_vec.erase(static_aggregate_vec.begin() + i);
                i--;
                continue;
            }
            static_aggregate_para_map[proc.specific_name] = para_vec;
            PQclear(res);
        }
        has_aggregate_para = true;
    }

    for (auto& proc:static_aggregate_vec) {
        register_aggregate(proc);
    }

    for (auto &proc : aggregates) {
        auto& para_vec = static_aggregate_para_map[proc.specific_name];
        for (auto t:para_vec) {
            proc.argtypes.push_back(t);
        }
    }

    generate_indexes();
}

schema_gaussdb::~schema_gaussdb()
{
}

gaussdb_connection::gaussdb_connection(string db, unsigned int port, string host, string user, string pass)
{
    test_db = db;
    test_port = port;
    host_addr = host;
    dbms_user = user;
    dbms_pass = pass;

    const char* user_ptr = dbms_user.empty() ? NULL : dbms_user.c_str();
    const char* pass_ptr = dbms_pass.empty() ? NULL : dbms_pass.c_str();

    conn = PQsetdbLogin(host_addr.c_str(), to_string(port).c_str(), NULL, NULL, db.c_str(), user_ptr, pass_ptr);
    if (PQstatus(conn) == CONNECTION_OK)
        return; // succeed

    string err = PQerrorMessage(conn);
    PQfinish(conn);
    conn = nullptr;

    // If connecting to the target database failed, try connecting to "postgres"
    // to determine if the server is reachable. If "postgres" works, the issue
    // is just that the target database doesn't exist yet — create it.
    PGconn *pg_conn = PQsetdbLogin(host_addr.c_str(), to_string(test_port).c_str(), NULL, NULL, "postgres", user_ptr, pass_ptr);
    if (PQstatus(pg_conn) != CONNECTION_OK) {
        // Server is not reachable — real connection failure
        string pg_err = PQerrorMessage(pg_conn);
        PQfinish(pg_conn);
        cerr << "[CONNECTION FAIL]  " << err << " in " << debug_info << endl;
        throw runtime_error("[CONNECTION FAIL] " + err + " in " + debug_info);
    }

    // Server is reachable — the target database likely doesn't exist. Create it.
    cerr << "try to create database " << test_db << endl;
    conn = pg_conn;

    string create_sql = "create database " + test_db + "; ";
    auto res = PQexec(conn, create_sql.c_str());
    auto status = PQresultStatus(res);
    if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK){
        string cerr_err = PQerrorMessage(conn);
        PQclear(res);
        throw runtime_error(cerr_err + " in " + debug_info);
    }
    PQclear(res);

    PQfinish(conn);
    conn = PQsetdbLogin(host_addr.c_str(), to_string(test_port).c_str(), NULL, NULL, test_db.c_str(), user_ptr, pass_ptr);
    if (PQstatus(conn) != CONNECTION_OK) {
        string cerr_err = PQerrorMessage(conn);
        cerr << cerr_err << " in " << debug_info << endl;
        throw runtime_error(cerr_err + " in " + debug_info);
    }
    cerr << "create successfully" << endl;
    return;
}

gaussdb_connection::~gaussdb_connection()
{
    PQfinish(conn);
}

dut_gaussdb::dut_gaussdb(string db, unsigned int port, string host, string user, string pass)
    : gaussdb_connection(db, port, host, user, pass)
{
    string set_timeout_cmd = "SET statement_timeout = '" + to_string(GAUSSDB_TIMEOUT_SECOND) + "s';";
    test(set_timeout_cmd, NULL, NULL);
}

static bool is_expected_error(string error)
{
    for (const auto& err : gaussdberrmsg)
        if (error.find(err) != string::npos)
            return true;

    // Crash-related patterns (connection lost = expected, framework handles restart)
    if (error.find("server closed the connection unexpectedly") != string::npos
        || error.find("connection to server was lost") != string::npos
        || error.find("terminating connection due to administrator command") != string::npos
        || error.find("no connection to the server") != string::npos)
        return true;

    // Common GaussDB error patterns
    if (error.find("invalid input syntax") != string::npos
        || error.find("does not exist") != string::npos
        || error.find("permission denied") != string::npos
        || error.find("division by zero") != string::npos
        || error.find("syntax error") != string::npos
        || error.find("canceling statement due to statement timeout") != string::npos
        || error.find("violates not-null constraint") != string::npos
        || error.find("violates unique constraint") != string::npos
        || error.find("violates foreign key constraint") != string::npos
        || error.find("violates check constraint") != string::npos
        || error.find("duplicate key value") != string::npos
        || error.find("out of range") != string::npos
        || error.find("cannot cast") != string::npos
        || error.find("operator does not exist") != string::npos
        || error.find("function does not exist") != string::npos
        || error.find("column does not exist") != string::npos
        || error.find("relation does not exist") != string::npos
        || error.find("ambiguous") != string::npos
        || error.find("more than one row returned") != string::npos
        || error.find("null value in column") != string::npos
        || error.find("value too long") != string::npos
        || error.find("numeric field overflow") != string::npos
        || error.find("integer out of range") != string::npos
        || error.find("smallint out of range") != string::npos
        || error.find("bigint out of range") != string::npos
        // Chinese error patterns (GaussDB sometimes returns Chinese errors)
        || error.find("\xe8\xaf\xad\xe6\xb3\x95\xe9\x94\x99\xe8\xaf\xaf") != string::npos  // 语法错误
        || error.find("\xe4\xb8\x8d\xe5\xad\x98\xe5\x9c\xa8") != string::npos              // 不存在
        || error.find("\xe6\x97\xa0\xe6\x9d\x83\xe9\x99\x90") != string::npos              // 无权限
        || error.find("\xe9\x99\xa4\xe9\x9b\xb6") != string::npos                          // 除零
        )
        return true;

    return false;
}

void dut_gaussdb::test(const string &stmt,
                    vector<vector<string>>* output,
                    int* affected_row_num,
                    vector<string>* env_setting_stmts)
{
    if (env_setting_stmts != NULL) {
        for (auto& set_statement : *env_setting_stmts) {
            auto res = PQexec(conn, set_statement.c_str());
            auto status = PQresultStatus(res);
            if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
                string err = PQerrorMessage(conn);
                PQclear(res);
                // clear the current result
                while (res != NULL) {
                    res = PQgetResult(conn);
                    PQclear(res);
                }
                throw runtime_error("[GAUSSDB] setting error [" + err + "]");
            }
        }
    }

    auto res = PQexec(conn, stmt.c_str());
    auto status = PQresultStatus(res);
    if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
        string err = PQerrorMessage(conn);
        PQclear(res);

        // clear the current result
        while (res != NULL) {
            res = PQgetResult(conn);
            PQclear(res);
        }

        // Crash detection: if connection is lost, report as BUG
        if (PQstatus(conn) == CONNECTION_BAD) {
            throw runtime_error("BUG!!! [GAUSSDB] connection lost: " + err + " in dut_gaussdb::test");
        }

        if (is_expected_error(err))
            throw runtime_error("[GAUSSDB] expected error [" + err + "]");
        else
            throw runtime_error("[GAUSSDB] execution error [" + err + "]");
    }

    if (affected_row_num) {
        auto char_num = PQcmdTuples(res);
        if (char_num != NULL)
            *affected_row_num = atoi(char_num);
        else
            *affected_row_num = 0;
    }

    if (output) {
        auto field_num = PQnfields(res);
        auto row_num = PQntuples(res);
        for (int i = 0; i < row_num; i++) {
            vector<string> row;
            for (int j = 0; j < field_num; j++) {
                auto tmp = PQgetvalue(res, i, j);
                string str;
                if (tmp == NULL)
                    str = "NULL";
                else {
                    auto res_unit = process_number_string(tmp);
                    str = res_unit;
                }
                row.push_back(str);
            }
            output->push_back(row);
        }
    }
    PQclear(res);

    return;
}

void dut_gaussdb::reset(void)
{
    const char* user_ptr = dbms_user.empty() ? NULL : dbms_user.c_str();
    const char* pass_ptr = dbms_pass.empty() ? NULL : dbms_pass.c_str();

    if (conn)
        PQfinish(conn);
    conn = PQsetdbLogin(host_addr.c_str(), to_string(test_port).c_str(), NULL, NULL, test_db.c_str(), user_ptr, pass_ptr);
    if (PQstatus(conn) != CONNECTION_OK) {
        string err = PQerrorMessage(conn);
        throw runtime_error(err + " in " + debug_info);
    }

    // Drop all user tables
    string drop_sql = "DO $$ DECLARE r RECORD; BEGIN FOR r IN (SELECT tablename FROM pg_tables WHERE schemaname = 'public') LOOP EXECUTE 'DROP TABLE IF EXISTS ' || quote_ident(r.tablename) || ' CASCADE'; END LOOP; END $$;";
    auto res = PQexec(conn, drop_sql.c_str());
    PQclear(res);

    // Drop all user sequences
    string drop_seq = "DO $$ DECLARE r RECORD; BEGIN FOR r IN (SELECT sequencename FROM pg_sequences WHERE schemaname = 'public') LOOP EXECUTE 'DROP SEQUENCE IF EXISTS ' || quote_ident(r.sequencename) || ' CASCADE'; END LOOP; END $$;";
    res = PQexec(conn, drop_seq.c_str());
    PQclear(res);

    // Drop all user indexes
    string drop_idx = "DO $$ DECLARE r RECORD; BEGIN FOR r IN (SELECT indexname FROM pg_indexes WHERE schemaname = 'public') LOOP EXECUTE 'DROP INDEX IF EXISTS ' || quote_ident(r.indexname) || ' CASCADE'; END LOOP; END $$;";
    res = PQexec(conn, drop_idx.c_str());
    PQclear(res);
}

void dut_gaussdb::backup(void)
{
    string bk_file = GAUSSDB_BK_FILE(test_db);
    string cp_cmd = "cp db_setup.sql " + bk_file;
    int ret = system(cp_cmd.c_str());
    if (ret != 0) {
        cerr << "backup fail: cannot copy db_setup.sql" << endl;
        throw runtime_error("backup fail in " + debug_info);
    }
}

void dut_gaussdb::reset_to_backup(void)
{
    reset();
    string bk_file = GAUSSDB_BK_FILE(test_db);
    if (access(bk_file.c_str(), F_OK ) == -1)
        return;

    const char* user_ptr = dbms_user.empty() ? NULL : dbms_user.c_str();
    const char* pass_ptr = dbms_pass.empty() ? NULL : dbms_pass.c_str();
    conn = PQsetdbLogin(host_addr.c_str(), to_string(test_port).c_str(), NULL, NULL, test_db.c_str(), user_ptr, pass_ptr);
    if (PQstatus(conn) != CONNECTION_OK) {
        string err = PQerrorMessage(conn);
        throw runtime_error("[CONNECTION FAIL] " + err + " in " + debug_info);
    }

    ifstream ifs(bk_file);
    if (!ifs.is_open()) {
        cerr << "reset_to_backup: cannot open " << bk_file << endl;
        return;
    }

    // Accumulate lines until a semicolon is found (SQL statements can span multiple lines)
    string accumulated;
    string line;
    while (getline(ifs, line)) {
        // Skip empty lines and comments
        if (line.empty() || line[0] == '-' || line[0] == '#')
            continue;
        accumulated += line + "\n";
        // Check if the accumulated string ends with semicolon (ignoring trailing whitespace)
        string trimmed = accumulated;
        while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\n' || trimmed.back() == '\t'))
            trimmed.pop_back();
        if (!trimmed.empty() && trimmed.back() == ';') {
            auto res = PQexec(conn, accumulated.c_str());
            if (PQresultStatus(res) != PGRES_COMMAND_OK && PQresultStatus(res) != PGRES_TUPLES_OK) {
                // Ignore non-fatal errors during restore
            }
            PQclear(res);
            accumulated.clear();
        }
    }
    if (!accumulated.empty()) {
        auto res = PQexec(conn, accumulated.c_str());
        PQclear(res);
    }
    ifs.close();
}

int dut_gaussdb::save_backup_file(string db_name, string path)
{
    string cp_cmd = "cp " + GAUSSDB_BK_FILE(db_name) + " " + path;
    return system(cp_cmd.c_str());
}

void dut_gaussdb::get_content(vector<string>& tables_name, map<string, vector<vector<string>>>& content)
{
    for (auto& table:tables_name) {
        vector<vector<string>> table_content;
        auto query = "SELECT * FROM " + table + " ORDER BY 1;";

        auto res = PQexec(conn, query.c_str());
        auto status = PQresultStatus(res);
        if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
            string err = PQerrorMessage(conn);
            PQclear(res);
            cerr << "Cannot get content of " + table + "\nLocation: " + debug_info << endl;
            cerr << "Error: " + err + "\nLocation: " + debug_info << endl;
            continue;
        }

        auto field_num = PQnfields(res);
        auto row_num = PQntuples(res);
        for (int i = 0; i < row_num; i++) {
            vector<string> row_output;
            for (int j = 0; j < field_num; j++) {
                auto tmp = PQgetvalue(res, i, j);
                string str;
                if (tmp == NULL)
                    str = "NULL";
                else {
                    auto res_unit = process_number_string(tmp);
                    str = res_unit;
                }
                row_output.push_back(str);
            }
            table_content.push_back(row_output);
        }
        PQclear(res);
        content[table] = table_content;
    }
    return;
}
