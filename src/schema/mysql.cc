#include "config.h"
#include <stdexcept>
#include <cassert>
#include <cstring>
#include "schema/mysql.hh"
#include <iostream>
#include <set>
#include <iomanip>

#ifndef HAVE_BOOST_REGEX
#include <regex>
#else
#include <boost/regex.hpp>
using boost::regex;
using boost::smatch;
using boost::regex_match;
#endif

using namespace std;

static regex e_unknown_database(".*Unknown database.*");
static regex e_db_dir_exists("[\\s\\S]*Schema directory[\\s\\S]*already exists. This must be resolved manually[\\s\\S]*");

static regex e_crash(".*Lost connection.*");
static regex e_dup_entry("Duplicate entry[\\s\\S]*for key[\\s\\S]*");
static regex e_large_results("Result of[\\s\\S]*was larger than max_allowed_packet[\\s\\S]*");
static regex e_timeout("Query execution was interrupted"); // timeout
static regex e_col_ambiguous("Column [\\s\\S]* in [\\s\\S]* is ambiguous");
static regex e_truncated("Truncated incorrect DOUBLE value:[\\s\\S]*");
static regex e_division_zero("Division by 0");
static regex e_unknown_col("Unknown column[\\s\\S]*"); // could be an unexpected error later
static regex e_incorrect_args("Incorrect arguments to[\\s\\S]*");
static regex e_out_of_range("[\\s\\S]*value is out of range[\\s\\S]*");
static regex e_win_context("You cannot use the window function[\\s\\S]*in this context[\\s\\S]*");
// same root cause of e_unknown_col, also could be an unexpected error later
static regex e_view_reference("[\\s\\S]*view lack rights to use them[\\s\\S]*");
static regex e_context_cancel("context canceled");
static regex e_string_convert("Cannot convert string[\\s\\S]*from binary to[\\s\\S]*");
static regex e_col_null("Column[\\s\\S]*cannot be null[\\s\\S]*");
static regex e_sridb_pk("Unsupported shard_row_id_bits for table with primary key as row id[\\s\\S]*");
static regex e_syntax("You have an error in your SQL syntax[\\s\\S]*");
static regex e_invalid_group("Invalid use of group function");
static regex e_invalid_group_2("In aggregated query without GROUP BY, expression[\\s\\S]*");
static regex e_oom("Out Of Memory Quota[\\s\\S]*");
static regex e_schema_changed("Information schema is changed during the execution of[\\s\\S]*");
static regex e_over_mem("[\\s\\S]*Your query has been cancelled due to exceeding the allowed memory limit for a single SQL query[\\s\\S]*");
static regex e_no_default("Field [\\s\\S]* doesn't have a default value");
static regex e_no_group_by("Expression [\\s\\S]* of SELECT list is not in GROUP BY clause and contains nonaggregated column[\\s\\S]*");
static regex e_no_support_1("[\\s\\S]* not supported [\\s\\S]*");
static regex e_no_support_2("This version of MySQL doesn't yet support [\\s\\S]*");
static regex e_invalid_arguement("Invalid argument for [\\s\\S]*");
static regex e_incorrect_string("Incorrect string value: [\\s\\S]*");
static regex e_long_specified_key("Specified key was too long; max key length is [\\s\\S]* bytes");
static regex e_out_of_range_2("Out of range value for column [\\s\\S]*");
static regex e_table_not_exists("Table [\\s\\S]* doesn't exist");
static regex e_ref_table_not_exists("Failed to open the referenced table [\\s\\S]*");
static regex e_subquery_transform("Statement requires a transform of a subquery[\\s\\S]*");
static regex e_fk_constraint_fail("Cannot add or update a child row: a foreign key constraint fails[\\s\\S]*");
static regex e_fk_missing_key("Failed to add the foreign key constraint[\\s\\S]*");


extern "C"  {
#include <unistd.h>
}

#define debug_info (string(__func__) + "(" + string(__FILE__) + ":" + to_string(__LINE__) + ")")

static int remove_dir(string dir) {
    string command = "rm -r " + dir;
    return system(command.c_str());
}

static bool is_double(string myString, long double& result) {
    istringstream iss(myString);
    iss >> noskipws >> result; // noskipws considers leading whitespace invalid
    // Check the entire string was consumed and if either failbit or badbit is set
    return iss.eof() && !iss.fail(); 
}

static string process_an_item(string& item)
{
    string final_str;
    long double result;
    if (is_double(item, result) == false) {
        final_str = item;
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

mysql_connection::mysql_connection(string db, unsigned int port,
                                   string host, string user, string pass)
{
    test_db = db;
    test_port = port;
    test_host = host;
    test_user = user;
    test_pass = pass;

    const char* pass_ptr = test_pass.empty() ? NULL : test_pass.c_str();

    if (!mysql_init(&mysql))
        throw std::runtime_error(string(mysql_error(&mysql)) + "\nLocation: " + debug_info);

    if (mysql_real_connect(&mysql, test_host.c_str(), test_user.c_str(), pass_ptr, test_db.c_str(), test_port, NULL, 0))
        return; // success

    string err = mysql_error(&mysql);
    if (!regex_match(err, e_unknown_database))
        throw std::runtime_error("BUG!!!" + string(mysql_error(&mysql)) + "\nLocation: " + debug_info);

    // error caused by unknown database, so create one
    cerr << test_db + " does not exist, use default db" << endl;
    if (!mysql_real_connect(&mysql, test_host.c_str(), test_user.c_str(), pass_ptr, NULL, port, NULL, 0))
        throw std::runtime_error(string(mysql_error(&mysql)) + "\nLocation: " + debug_info);
    
    cerr << "create database " + test_db << endl;
    string create_sql = "create database " + test_db + "; ";
    if (mysql_real_query(&mysql, create_sql.c_str(), create_sql.size())) {
        auto create_db_err = string(mysql_error(&mysql));
        if (!regex_match(create_db_err, e_db_dir_exists))
            throw std::runtime_error(string(mysql_error(&mysql)) + "\nLocation: " + debug_info);

        cerr << "schema directory " << test_db << " already exists. Remove it" << endl;
        string schema_dir = "/usr/local/mysql/data/" + test_db;
        int status = remove_dir(schema_dir);
        if (status != 0)
            throw runtime_error("error removing directory.");

        cerr << "directory removed, create the database again" << endl;
        if (mysql_real_query(&mysql, create_sql.c_str(), create_sql.size())) 
            throw runtime_error(string(mysql_error(&mysql)) + "\nLocation: " + debug_info);
    }
        
    auto res = mysql_store_result(&mysql);
    mysql_free_result(res);

    cerr << "use database " + test_db << endl;
    string use_sql = "use " + test_db + "; ";
    if (mysql_real_query(&mysql, use_sql.c_str(), use_sql.size()))
        throw std::runtime_error(string(mysql_error(&mysql)) + "\nLocation: " + debug_info);
    res = mysql_store_result(&mysql);
    mysql_free_result(res);
}

mysql_connection::~mysql_connection()
{
    mysql_close(&mysql);
}

schema_mysql::schema_mysql(string db, unsigned int port,
                           string host, string user, string pass)
  : mysql_connection(db, port, host, user, pass)
{
    target_dbms = "mysql";

    // ── Canonical types (must be created BEFORE aliases and column loading) ──
    booltype = sqltype::get("tinyint");
    inttype = sqltype::get("int");
    realtype = sqltype::get("double");
    texttype = sqltype::get("varchar(200)");
    datetype = sqltype::get("DATETIME");

    // ── Type aliasing: map all MySQL DATA_TYPE variants to canonical types ──
    // This ensures columns with BIGINT/FLOAT/TEXT etc. are compatible with
    // functions/operators defined on inttype/realtype/texttype.
    // Reference: sql_yacc.yy field_type: rule (~line 12500) for complete type list

    // Integer types → inttype
    for (auto &alias : {"bigint", "mediumint", "smallint", "tinyint", "integer"})
        sqltype::typemap[alias] = inttype;

    // Real/float types → realtype
    for (auto &alias : {"float", "decimal", "numeric", "real"})
        sqltype::typemap[alias] = realtype;

    // Text types → texttype
    for (auto &alias : {"varchar", "char", "text", "mediumtext", "longtext", "tinytext", "enum", "set"})
        sqltype::typemap[alias] = texttype;

    // Date/time types → datetype
    for (auto &alias : {"datetime", "timestamp", "date", "time", "year"})
        sqltype::typemap[alias] = datetype;

    // Boolean → booltype
    sqltype::typemap["boolean"] = booltype;
    sqltype::typemap["bool"] = booltype;

    // BLOB/binary types → blobtype
    auto blobtype = sqltype::get("blob");
    for (auto &alias : {"tinyblob", "mediumblob", "longblob", "binary", "varbinary"})
        sqltype::typemap[alias] = blobtype;

    // ── Loading tables ──
    string get_table_query = "SELECT DISTINCT TABLE_NAME FROM INFORMATION_SCHEMA.TABLES \
        WHERE TABLE_SCHEMA='" + db + "' AND \
              TABLE_TYPE='BASE TABLE' ORDER BY 1;";
    
    if (mysql_real_query(&mysql, get_table_query.c_str(), get_table_query.size()))
        throw std::runtime_error(string(mysql_error(&mysql)) + "\nLocation: " + debug_info);
    
    auto result = mysql_store_result(&mysql);
    while (auto row = mysql_fetch_row(result)) {
        table tab(row[0], "main", true, true);
        tables.push_back(tab);
    }
    mysql_free_result(result);

    // Loading views...;
    string get_view_query = "select distinct table_name from information_schema.views \
        where table_schema='" + db + "' order by 1;";
    if (mysql_real_query(&mysql, get_view_query.c_str(), get_view_query.size()))
        throw std::runtime_error(string(mysql_error(&mysql)) + "\nLocation: " + debug_info);
    
    result = mysql_store_result(&mysql);
    while (auto row = mysql_fetch_row(result)) {
        table tab(row[0], "main", false, false);
        tables.push_back(tab);
    }
    mysql_free_result(result);

    // Loading indexes...;
    string get_index_query = "SELECT DISTINCT INDEX_NAME FROM INFORMATION_SCHEMA.STATISTICS \
                WHERE TABLE_SCHEMA='" + db + "' AND \
                    NON_UNIQUE=1 AND \
                    INDEX_NAME <> COLUMN_NAME AND \
                    INDEX_NAME <> 'PRIMARY' ORDER BY 1;";
    if (mysql_real_query(&mysql, get_index_query.c_str(), get_index_query.size()))
        throw std::runtime_error(string(mysql_error(&mysql)) + "\nLocation: " + debug_info);

    result = mysql_store_result(&mysql);
    while (auto row = mysql_fetch_row(result)) {
        indexes.push_back(row[0]);
    }
    mysql_free_result(result);

    // Loading columns and constraints...;
    for (auto& t : tables) {
        string get_column_query = "SELECT COLUMN_NAME, DATA_TYPE FROM INFORMATION_SCHEMA.COLUMNS \
                WHERE TABLE_NAME='" + t.ident() + "' AND \
                    TABLE_SCHEMA='" + db + "'  ORDER BY ORDINAL_POSITION;";
        if (mysql_real_query(&mysql, get_column_query.c_str(), get_column_query.size()))
            throw std::runtime_error(string(mysql_error(&mysql)) + "\nLocation: " + debug_info + "\nTable: " + t.ident());
        result = mysql_store_result(&mysql);
        while (auto row = mysql_fetch_row(result)) {
            column c(row[0], sqltype::get(row[1]));
            t.columns().push_back(c);
        }
        mysql_free_result(result);
    }

    // ── Compound operators ──
    compound_operators.push_back("union distinct");
    compound_operators.push_back("union all");
    // MySQL 8.0.31+ supports INTERSECT and EXCEPT
    compound_operators.push_back("intersect");
    compound_operators.push_back("except");

    supported_join_op.push_back("inner");
    supported_join_op.push_back("left outer");
    supported_join_op.push_back("right outer");
    supported_join_op.push_back("cross");
    supported_join_op.push_back("straight_join");  // MySQL-specific
    supported_join_op.push_back("natural");         // Sprint 5: NATURAL JOIN

    // ── Binary operators ──
    BINOP(&, inttype, inttype, inttype);
    BINOP(>>, inttype, inttype, inttype);
    BINOP(<<, inttype, inttype, inttype);
    BINOP(^, inttype, inttype, inttype);
    BINOP(|, inttype, inttype, inttype);

    // comparison
    BINOP(>, inttype, inttype, booltype);
    BINOP(>, texttype, texttype, booltype);
    BINOP(>, realtype, realtype, booltype);
    BINOP(<, inttype, inttype, booltype);
    BINOP(<, texttype, texttype, booltype);
    BINOP(<, realtype, realtype, booltype);
    BINOP(>=, inttype, inttype, booltype);
    BINOP(>=, texttype, texttype, booltype);
    BINOP(>=, realtype, realtype, booltype);
    BINOP(<=, inttype, inttype, booltype);
    BINOP(<=, texttype, texttype, booltype);
    BINOP(<=, realtype, realtype, booltype);
    BINOP(<>, inttype, inttype, booltype);
    BINOP(<>, texttype, texttype, booltype);
    BINOP(<>, realtype, realtype, booltype);
    BINOP(!=, inttype, inttype, booltype);
    BINOP(!=, texttype, texttype, booltype);
    BINOP(!=, realtype, realtype, booltype);
    BINOP(<=>, inttype, inttype, booltype);
    BINOP(<=>, texttype, texttype, booltype);
    BINOP(<=>, realtype, realtype, booltype);
    BINOP(=, inttype, inttype, booltype);
    BINOP(=, texttype, texttype, booltype);
    BINOP(=, realtype, realtype, booltype);

    // arithmetic
    BINOP(%, inttype, inttype, inttype);
    BINOP(%, realtype, realtype, realtype);
    BINOP(*, inttype, inttype, inttype);
    BINOP(*, realtype, realtype, realtype);
    BINOP(+, inttype, inttype, inttype);
    BINOP(+, realtype, realtype, realtype);
    BINOP(-, inttype, inttype, inttype);
    BINOP(-, realtype, realtype, realtype);
    BINOP(/, inttype, inttype, inttype);
    BINOP(/, realtype, realtype, realtype);
    BINOP(DIV, inttype, inttype, inttype);
    BINOP(DIV, realtype, realtype, realtype);

    // logic
    BINOP(&&, booltype, booltype, booltype);
    BINOP(||, booltype, booltype, booltype);
    BINOP(XOR, booltype, booltype, booltype);

    // comparison function
    FUNC3(GREATEST, inttype, inttype, inttype, inttype);
    FUNC3(GREATEST, realtype, realtype, realtype, realtype);
    FUNC3(LEAST, inttype, inttype, inttype, inttype);
    FUNC3(LEAST, realtype, realtype, realtype, realtype);
    FUNC3(INTERVAL, inttype, inttype, inttype, inttype);
    FUNC3(INTERVAL, inttype, realtype, realtype, realtype);

    // function
    FUNC1(ABS, inttype, inttype);
    FUNC1(ABS, realtype, realtype);
    FUNC1(ACOS, realtype, inttype);
    FUNC1(ACOS, realtype, realtype);
    FUNC1(ASIN, realtype, inttype);
    FUNC1(ASIN, realtype, realtype);
    FUNC1(ATAN, realtype, inttype);
    FUNC1(ATAN, realtype, realtype);
    FUNC2(ATAN, realtype, inttype, inttype);
    FUNC2(ATAN, realtype, realtype, realtype);
    FUNC2(ATAN2, realtype, inttype, inttype);
    FUNC2(ATAN2, realtype, realtype, realtype);
    FUNC1(CEILING, inttype, realtype);
    FUNC1(COS, realtype, inttype);
    FUNC1(COS, realtype, realtype);
    FUNC1(COT, realtype, inttype);
    FUNC1(COT, realtype, realtype);
    FUNC1(CRC32, realtype, texttype);
    FUNC1(DEGREES, realtype, inttype);
    FUNC1(DEGREES, realtype, realtype);
    FUNC1(EXP, realtype, inttype);
    FUNC1(EXP, realtype, realtype);
    FUNC1(FLOOR, inttype, realtype);
    FUNC1(LN, realtype, inttype);
    FUNC1(LN, realtype, realtype);
    FUNC1(LOG, realtype, inttype);
    FUNC1(LOG, realtype, realtype);
    FUNC2(LOG, realtype, inttype, inttype);
    FUNC2(LOG, realtype, realtype, realtype);
    FUNC1(LOG2, realtype, inttype);
    FUNC1(LOG2, realtype, realtype);
    FUNC1(LOG10, realtype, inttype);
    FUNC1(LOG10, realtype, realtype);
    FUNC(PI, realtype);
    FUNC2(POW, inttype, inttype, inttype);
    FUNC2(POW, realtype, realtype, realtype);
    FUNC1(RADIANS, realtype, inttype);
    FUNC1(RADIANS, realtype, realtype);
    FUNC1(ROUND, inttype, realtype);
    FUNC1(SIGN, inttype, inttype);
    FUNC1(SIGN, inttype, realtype);
    FUNC1(SIN, realtype, inttype);
    FUNC1(SIN, realtype, realtype);
    FUNC1(SQRT, realtype, inttype);
    FUNC1(SQRT, realtype, realtype);
    FUNC1(TAN, realtype, inttype);
    FUNC1(TAN, realtype, realtype);
    FUNC2(TRUNCATE, realtype, realtype, inttype);

    // datetime operation
    FUNC2(ADDDATE, datetype, datetype, inttype);
    FUNC2(DATEDIFF, inttype, datetype, datetype);
    // FUNC1(DATENAME, texttype, datetype); //FUNCTION testdb1.DATENAME does not exist
    FUNC1(DAYOFMONTH, inttype, datetype);
    FUNC1(DAYOFWEEK, inttype, datetype);
    FUNC1(DAYOFYEAR, inttype, datetype);
    FUNC1(HOUR, inttype, datetype);
    FUNC1(MINUTE, inttype, datetype);
    FUNC1(MONTH, inttype, datetype);
    FUNC1(MONTHNAME, texttype, datetype);
    FUNC1(QUARTER, inttype, datetype);
    FUNC1(SECOND, inttype, datetype);
    FUNC2(SUBDATE, datetype, datetype, inttype);
    FUNC1(TIME_TO_SEC, inttype, datetype);
    FUNC1(TO_DAYS, inttype, datetype);
    FUNC1(TO_SECONDS, inttype, datetype);
    FUNC1(UNIX_TIMESTAMP, inttype, datetype);
    FUNC1(WEEK, inttype, datetype);
    FUNC1(WEEKDAY, inttype, datetype);
    FUNC1(WEEKOFYEAR, inttype, datetype);
    FUNC1(YEAR, inttype, datetype);
    FUNC1(YEARWEEK, inttype, datetype);

    // string functions
    FUNC1(ASCII, inttype, texttype);
    FUNC1(BIN, texttype, inttype);
    FUNC1(BIT_LENGTH, inttype, texttype);
    FUNC1(CHAR_LENGTH, inttype, texttype);
    FUNC2(CONCAT, texttype, texttype, texttype);
    FUNC4(FIELD, inttype, texttype, texttype, texttype, texttype);
    FUNC2(LEFT, texttype, texttype, inttype);
    FUNC1(LENGTH, inttype, texttype);
    FUNC1(HEX, texttype, texttype);
    FUNC1(HEX, texttype, inttype);
    FUNC2(INSTR, inttype, texttype, texttype);
    FUNC2(LOCATE, inttype, texttype, texttype);
    FUNC1(LOWER, texttype, texttype);
    FUNC3(LPAD, texttype, texttype, inttype, texttype);
    FUNC1(LTRIM, texttype, texttype);
    FUNC4(MAKE_SET, texttype, inttype, texttype, texttype, texttype);
    FUNC1(OCT, texttype, inttype);
    FUNC1(ORD, inttype, texttype);
    FUNC1(QUOTE, texttype, texttype);
    FUNC2(REPEAT, texttype, texttype, inttype);
    FUNC3(REPLACE, texttype, texttype, texttype, texttype);
    FUNC1(REVERSE, texttype, texttype);
    FUNC2(RIGHT, texttype, texttype, inttype);
    FUNC3(RPAD, texttype, texttype, inttype, texttype);
    FUNC1(RTRIM, texttype, texttype);
    FUNC1(SOUNDEX, texttype, texttype);
    FUNC1(SPACE, texttype, inttype);
    FUNC3(SUBSTRING, texttype, texttype, inttype, inttype);
    FUNC1(TO_BASE64, texttype, texttype);
    FUNC1(TRIM, texttype, texttype);
    // FUNC1(UNHEX, texttype, texttype); it return a binary string, which is a different type from string. 
    // In case when, string and binary string will become binary string 
    FUNC1(UPPER, texttype, texttype);
    FUNC2(STRCMP, inttype, texttype, texttype);
    // FUNC1(CHAR, texttype, inttype);

    // bit function
    FUNC1(BIT_COUNT, inttype, inttype);

    // MySQL-specific conditional functions (from sql_yacc.yy simple_expr)
    FUNC3(IF, inttype, booltype, inttype, inttype);
    FUNC3(IF, realtype, booltype, realtype, realtype);
    FUNC3(IF, texttype, booltype, texttype, texttype);
    FUNC2(IFNULL, inttype, inttype, inttype);
    FUNC2(IFNULL, realtype, realtype, realtype);
    FUNC2(IFNULL, texttype, texttype, texttype);
    FUNC1(ISNULL, booltype, inttype);
    FUNC1(ISNULL, booltype, realtype);
    FUNC1(ISNULL, booltype, texttype);
    FUNC2(NULLIF, inttype, inttype, inttype);
    FUNC2(NULLIF, realtype, realtype, realtype);
    FUNC2(NULLIF, texttype, texttype, texttype);

    // MySQL string functions (from sql_yacc.yy function_call_*)
    FUNC3(CONCAT_WS, texttype, texttype, texttype, texttype);
    FUNC1(MD5, texttype, texttype);
    FUNC1(SHA1, texttype, texttype);
    FUNC1(SHA2, texttype, texttype);
    FUNC2(FIND_IN_SET, inttype, texttype, texttype);
    FUNC1(CHAR_LENGTH, inttype, texttype);  // duplicate guard via sqltype::get
    FUNC2(FORMAT, texttype, realtype, inttype);
    FUNC1(UCASE, texttype, texttype);
    FUNC1(LCASE, texttype, texttype);

    // ── JSON type and functions (Sprint 2) ──
    auto jsontype = sqltype::get("json");
    sqltype::typemap["json"] = jsontype;

    // JSON creation functions
    FUNC2(JSON_ARRAY, jsontype, texttype, texttype);
    FUNC2(JSON_OBJECT, jsontype, texttype, texttype);
    FUNC1(JSON_QUOTE, jsontype, texttype);

    // JSON search functions
    FUNC2(JSON_EXTRACT, jsontype, jsontype, texttype);
    FUNC2(JSON_CONTAINS, booltype, jsontype, jsontype);
    FUNC1(JSON_KEYS, jsontype, jsontype);

    // JSON modification functions
    FUNC3(JSON_SET, jsontype, jsontype, texttype, texttype);
    FUNC3(JSON_INSERT, jsontype, jsontype, texttype, texttype);
    FUNC3(JSON_REPLACE, jsontype, jsontype, texttype, texttype);
    FUNC2(JSON_REMOVE, jsontype, jsontype, texttype);
    FUNC2(JSON_MERGE_PATCH, jsontype, jsontype, jsontype);

    // JSON attribute functions
    FUNC1(JSON_DEPTH, inttype, jsontype);
    FUNC1(JSON_LENGTH, inttype, jsontype);
    FUNC1(JSON_TYPE, texttype, jsontype);
    FUNC1(JSON_UNQUOTE, texttype, jsontype);

    // ── Date/Time functions (Sprint 4) ──
    FUNC(NOW, datetype);
    FUNC(CURDATE, datetype);
    FUNC(CURTIME, texttype);
    FUNC(SYSDATE, datetype);
    FUNC(UTC_TIMESTAMP, datetype);
    FUNC(UTC_DATE, datetype);
    FUNC(UTC_TIME, texttype);
    FUNC2(DATE_FORMAT, texttype, datetype, texttype);
    FUNC2(STR_TO_DATE, datetype, texttype, texttype);
    FUNC1(LAST_DAY, datetype, datetype);
    FUNC2(MAKEDATE, datetype, inttype, inttype);
    FUNC3(MAKETIME, texttype, inttype, inttype, inttype);

    // ── Information functions (Sprint 4) ──
    FUNC(VERSION, texttype);
    FUNC(DATABASE, texttype);
    FUNC(USER, texttype);
    FUNC(CONNECTION_ID, inttype);
    FUNC(LAST_INSERT_ID, inttype);
    FUNC(FOUND_ROWS, inttype);
    FUNC(ROW_COUNT, inttype);

    // ── Regex string functions (Sprint 4) ──
    FUNC3(REGEXP_REPLACE, texttype, texttype, texttype, texttype);
    FUNC2(REGEXP_INSTR, inttype, texttype, texttype);
    FUNC2(REGEXP_SUBSTR, texttype, texttype, texttype);

    // aggregate functions
    AGG1(AVG, realtype, inttype);
    AGG1(AVG, realtype, realtype);
    AGG1(BIT_AND, inttype, inttype);
    AGG1(BIT_OR, inttype, inttype);
    AGG1(BIT_XOR, inttype, inttype);
    AGG(COUNT, inttype);
    AGG1(COUNT, inttype, realtype);
    AGG1(COUNT, inttype, texttype);
    AGG1(COUNT, inttype, inttype);
    AGG1(MAX, realtype, realtype);
    AGG1(MAX, inttype, inttype);
    AGG1(MIN, realtype, realtype);
    AGG1(MIN, inttype, inttype);
    AGG1(STDDEV_POP, realtype, realtype);
    AGG1(STDDEV_POP, realtype, inttype);
    AGG1(STDDEV_SAMP, realtype, realtype);
    AGG1(STDDEV_SAMP, realtype, inttype);
    AGG1(SUM, realtype, realtype);
    AGG1(SUM, inttype, inttype);
    AGG1(VAR_POP, realtype, realtype);
    AGG1(VAR_POP, realtype, inttype);
    AGG1(VAR_SAMP, realtype, realtype);
    AGG1(VAR_SAMP, realtype, inttype);

    // MySQL-specific aggregates
    AGG1(ANY_VALUE, inttype, inttype);
    AGG1(ANY_VALUE, realtype, realtype);
    AGG1(ANY_VALUE, texttype, texttype);
    AGG1(GROUP_CONCAT, texttype, texttype);
    AGG1(GROUP_CONCAT, texttype, inttype);
    AGG1(JSON_ARRAYAGG, texttype, inttype);
    AGG1(JSON_ARRAYAGG, texttype, texttype);
    AGG1(JSON_OBJECTAGG, texttype, texttype);

    // ranking window function
    WIN(CUME_DIST, realtype);
    WIN(DENSE_RANK, inttype);
    WIN1(NTILE, inttype, inttype);
    WIN(RANK, inttype);
    WIN(ROW_NUMBER, inttype);
    WIN(PERCENT_RANK, realtype);

    // value window function
    WIN1(FIRST_VALUE, inttype, inttype);
    WIN1(FIRST_VALUE, realtype, realtype);
    WIN1(FIRST_VALUE, texttype, texttype);
    WIN1(LAST_VALUE, inttype, inttype);
    WIN1(LAST_VALUE, realtype, realtype);
    WIN1(LAST_VALUE, texttype, texttype);
    WIN1(LAG, inttype, inttype);
    WIN1(LAG, realtype, realtype);
    WIN1(LAG, texttype, texttype);
    WIN1(LEAD, inttype, inttype);
    WIN1(LEAD, realtype, realtype);
    WIN1(LEAD, texttype, texttype);

    // Sprint 3: NTH_VALUE (2 parameters: expr, N)
    WIN2(NTH_VALUE, inttype, inttype, inttype);
    WIN2(NTH_VALUE, realtype, realtype, inttype);
    WIN2(NTH_VALUE, texttype, texttype, inttype);

    internaltype = sqltype::get("internal");
    arraytype = sqltype::get("ARRAY");

    true_literal = "true";
    false_literal = "false";

    // MySQL 8.x optimizer_switch options for EET env_setting_stmts
    // set_stmt::out() generates: SET SESSION optimizer_switch = 'flag=on/off'
    vector<string> optimizer_flags = {
        "batch_key_access", "block_nested_loop", "condition_fanout_filter",
        "derived_merge", "duplicateweedout", "firstmatch",
        "index_condition_pushdown", "index_merge",
        "index_merge_intersection", "index_merge_union",
        "index_merge_sort_union", "loosescan", "materialization",
        "mrr", "mrr_cost_based", "semijoin",
        "subquery_materialization_cost_based",
        "use_index_extensions"
    };
    for (auto& flag : optimizer_flags) {
        supported_setting["SESSION optimizer_switch"]
            .push_back("'" + flag + "=on'");
        supported_setting["SESSION optimizer_switch"]
            .push_back("'" + flag + "=off'");
    }

    // Set MySQL feature flags
    features.has_on_duplicate_key = true;
    features.has_if_function      = true;
    features.has_group_concat     = true;
    features.has_returning        = false;  // MySQL does not support RETURNING
    features.has_upsert           = true;   // via ON DUPLICATE KEY UPDATE
    features.has_lateral          = true;   // MySQL 8.0.14+
    features.has_for_update       = true;
    features.has_full_outer_join  = false;  // MySQL does not support FULL OUTER JOIN
    features.has_intersect_except = false;  // MySQL 8.0.31+ (version-dependent)
    features.has_regexp           = true;   // MySQL REGEXP/RLIKE
    features.has_sounds_like      = true;   // MySQL SOUNDS LIKE
    features.has_straight_join    = true;   // MySQL STRAIGHT_JOIN
    features.has_index_hints      = true;   // USE/FORCE/IGNORE INDEX
    features.has_with_rollup      = true;   // GROUP BY ... WITH ROLLUP
    features.has_replace          = true;   // REPLACE statement
    features.has_do_stmt          = true;   // DO statement
    features.has_explain          = true;   // EXPLAIN / EXPLAIN ANALYZE
    features.has_select_options   = true;   // SQL_CALC_FOUND_ROWS etc.

    // Sprint 1: Storage engines + table options
    supported_table_engine.push_back("InnoDB");
    supported_table_engine.push_back("MyISAM");
    supported_table_engine.push_back("MEMORY");

    available_table_options.push_back("CHARSET=utf8mb4");
    available_table_options.push_back("CHARSET=latin1");
    available_table_options.push_back("ROW_FORMAT=DYNAMIC");
    available_table_options.push_back("ROW_FORMAT=COMPRESSED");

    // Sprint 1: Enable CAST and INTERVAL
    features.has_cast             = true;
    features.has_interval_expr    = true;

    // Sprint 2: Enable MySQL JSON
    features.has_mysql_json       = true;

    // Sprint 3: Enable window frame (MySQL 8.0+ supports ROWS/RANGE/GROUPS)
    features.has_window_frame     = true;

    // Sprint 4: Enable SAVEPOINT
    features.has_savepoint        = true;

    // Partition table support
    features.has_partition_table    = true;
    features.has_subpartition       = true;
    features.has_partition_mgmt     = true;
    features.has_partition_select   = true;

    // Sprint 4: sql_mode SET parameters
    supported_setting["SESSION sql_mode"] = {
        "'ONLY_FULL_GROUP_BY'",
        "'STRICT_TRANS_TABLES'",
        "'NO_ZERO_IN_DATE'",
        "'ERROR_FOR_DIVISION_BY_ZERO'",
        "'NO_ENGINE_SUBSTITUTION'",
        "'PIPES_AS_CONCAT'"
    };

    generate_indexes();
}

void schema_mysql::update_schema()
{
    // Loading tables...;
    string get_table_query = "SELECT DISTINCT TABLE_NAME FROM INFORMATION_SCHEMA.TABLES \
        WHERE TABLE_SCHEMA='" + test_db + "' AND \
              TABLE_TYPE='BASE TABLE' ORDER BY 1;";
    
    if (mysql_real_query(&mysql, get_table_query.c_str(), get_table_query.size()))
        throw std::runtime_error(string(mysql_error(&mysql)) + "\nLocation: " + debug_info);
    
    auto result = mysql_store_result(&mysql);
    while (auto row = mysql_fetch_row(result)) {
        table tab(row[0], "main", true, true);
        tables.push_back(tab);
    }
    mysql_free_result(result);

    // Loading views...;
    string get_view_query = "select distinct table_name from information_schema.views \
        where table_schema='" + test_db + "' order by 1;";
    if (mysql_real_query(&mysql, get_view_query.c_str(), get_view_query.size()))
        throw std::runtime_error(string(mysql_error(&mysql)) + "\nLocation: " + debug_info);
    
    result = mysql_store_result(&mysql);
    while (auto row = mysql_fetch_row(result)) {
        table tab(row[0], "main", false, false);
        tables.push_back(tab);
    }
    mysql_free_result(result);

    // Loading indexes...;
    string get_index_query = "SELECT DISTINCT INDEX_NAME FROM INFORMATION_SCHEMA.STATISTICS \
                WHERE TABLE_SCHEMA='" + test_db + "' AND \
                    NON_UNIQUE=1 AND \
                    INDEX_NAME <> COLUMN_NAME AND \
                    INDEX_NAME <> 'PRIMARY' ORDER BY 1;";
    if (mysql_real_query(&mysql, get_index_query.c_str(), get_index_query.size()))
        throw std::runtime_error(string(mysql_error(&mysql)) + "\nLocation: " + debug_info);

    result = mysql_store_result(&mysql);
    while (auto row = mysql_fetch_row(result)) {
        indexes.push_back(row[0]);
    }
    mysql_free_result(result);

    // Loading columns and constraints...;
    for (auto& t : tables) {
        string get_column_query = "SELECT COLUMN_NAME, DATA_TYPE FROM INFORMATION_SCHEMA.COLUMNS \
                WHERE TABLE_NAME='" + t.ident() + "' AND \
                    TABLE_SCHEMA='" + test_db + "'  ORDER BY ORDINAL_POSITION;";
        if (mysql_real_query(&mysql, get_column_query.c_str(), get_column_query.size()))
            throw std::runtime_error(string(mysql_error(&mysql)) + "\nLocation: " + debug_info + "\nTable: " + t.ident());
        result = mysql_store_result(&mysql);
        while (auto row = mysql_fetch_row(result)) {
            column c(row[0], sqltype::get(row[1]));
            t.columns().push_back(c);
        }
        mysql_free_result(result);
    }
    return;
}

dut_mysql::dut_mysql(string db, unsigned int port,
                     string host, string user, string pass)
  : mysql_connection(db, port, host, user, pass)
{
    sent_sql = "";
    has_sent_sql = false;
    txn_abort = false;
    thread_id = mysql_thread_id(&mysql);
#ifdef HAVE_MYSQL_NONBLOCK
    block_test("SET SESSION TRANSACTION ISOLATION LEVEL REPEATABLE READ;");
#endif
}

#ifdef HAVE_MYSQL_NONBLOCK
static unsigned long long get_cur_time_ms(void) {
	struct timeval tv;
	struct timezone tz;

	gettimeofday(&tv, &tz);

	return (tv.tv_sec * 1000ULL) + tv.tv_usec / 1000;
}

bool dut_mysql::check_whether_block(vector<unsigned long>& blocking_tids)
{
    dut_mysql another_dut(test_db, test_port);
    string get_block_tid = "SELECT blocking_pid FROM sys.innodb_lock_waits where waiting_pid = " + to_string(thread_id) + ";";
    vector<vector<string>> output;
    another_dut.block_test(get_block_tid, &output);
    
    bool find_blocked_tid = false; 
    for (int i = 0; i < output.size(); i++) {
        for (int j = 0; j < output[i].size(); j++) {
            auto blocking_tid = stoi(output[i][j]);
            find_blocked_tid = true;
            blocking_tids.push_back(blocking_tid);
        }
    }

    return find_blocked_tid;
}
#endif

void dut_mysql::block_test(const string &stmt,
    vector<vector<string>>* output, 
    int* affected_row_num)
{
    if (mysql_real_query(&mysql, stmt.c_str(), stmt.size())) {
        string err = mysql_error(&mysql);
        auto result = mysql_store_result(&mysql);
        mysql_free_result(result);
        if (err.find("Commands out of sync") != string::npos) {// occasionally happens, retry the statement again
            cerr << err << " in test, repeat the statement again" << endl;
            block_test(stmt, output, affected_row_num);
            return;
        }
        if (regex_match(err, e_crash)) {
            throw std::runtime_error("BUG!!! " + err + " in mysql::block_test"); 
        }
        string prefix = "mysql block_test expected error:";
        if (regex_match(err, e_dup_entry) 
            || regex_match(err, e_large_results) 
            || regex_match(err, e_timeout) 
            || regex_match(err, e_col_ambiguous)
            || regex_match(err, e_truncated) 
            || regex_match(err, e_division_zero)
            || regex_match(err, e_unknown_col) 
            || regex_match(err, e_incorrect_args)
            || regex_match(err, e_out_of_range) 
            || regex_match(err, e_win_context)
            || regex_match(err, e_view_reference) 
            || regex_match(err, e_context_cancel)
            || regex_match(err, e_string_convert)
            // || regex_match(err, e_idx_oor)
            || regex_match(err, e_col_null)
            || regex_match(err, e_sridb_pk)
            || regex_match(err, e_syntax)
            // || regex_match(err, e_expr_pushdown)
            || regex_match(err, e_invalid_group)
            || regex_match(err, e_invalid_group_2)
            || regex_match(err, e_oom)
            // || regex_match(err, e_cannot_column)
            || regex_match(err, e_schema_changed)
            // || regex_match(err, e_invalid_addr)
            // || regex_match(err, e_makeslice)
            // || regex_match(err, e_undef_win)
            || regex_match(err, e_over_mem)
            || regex_match(err, e_no_default)
            || regex_match(err, e_no_group_by)
            || regex_match(err, e_no_support_1)
            || regex_match(err, e_no_support_2)
            || regex_match(err, e_invalid_arguement)
            || regex_match(err, e_incorrect_string)
            || regex_match(err, e_long_specified_key)
            || regex_match(err, e_out_of_range_2)
            || regex_match(err, e_table_not_exists)
            || regex_match(err, e_ref_table_not_exists)
            || regex_match(err, e_subquery_transform)
            || regex_match(err, e_fk_constraint_fail)
            || regex_match(err, e_fk_missing_key)
           ) {
            throw runtime_error(prefix + err);
        }

        throw std::runtime_error("[" + err + "] in mysql::block_test"); 
    }

    if (affected_row_num)
        *affected_row_num = mysql_affected_rows(&mysql);

    auto result = mysql_store_result(&mysql);
    if (mysql_errno(&mysql) != 0) {
        string err = mysql_error(&mysql);
        if (regex_match(err, e_out_of_range)
            || regex_match(err, e_string_convert)
            || regex_match(err, e_table_not_exists)
            ) {
            throw runtime_error("mysql block_test/mysql_store_result expected error: " + err);
        }

        throw std::runtime_error("block_test: mysql_store_result fails [" + err + "]\nLocation: " + debug_info); 
    }

    if (output && result) {
        auto row_num = mysql_num_rows(result);
        if (row_num == 0) {
            mysql_free_result(result);
            return;
        }

        auto column_num = mysql_num_fields(result);
        while (auto row = mysql_fetch_row(result)) {
            vector<string> row_output;
            for (int i = 0; i < column_num; i++) {
                string str;
                if (row[i] == NULL)
                    str = "NULL";
                else
                    str = row[i];
                row_output.push_back(process_an_item(str));
            }
            output->push_back(row_output);
        }
    }
    mysql_free_result(result);

    return;
}

#ifdef HAVE_MYSQL_NONBLOCK
void dut_mysql::test(const string &stmt,
    vector<vector<string>>* output,
    int* affected_row_num,
    vector<string>* env_setting_stmts)
{
#ifdef NOT_TEST_TXN
    return block_test(stmt, output, affected_row_num);
#endif
    net_async_status status;
    if (txn_abort == true) {
        auto tmp_stmt = stmt;
        if (stmt == "COMMIT;") 
            throw std::runtime_error("txn aborted, can only rollback \nLocation: " + debug_info);
        if (stmt == "ROLLBACK;")
            return;
        throw std::runtime_error("txn aborted, stmt skipped \nLocation: " + debug_info);
    }

    if (has_sent_sql == false) {
        auto clear_results = mysql_store_result(&mysql);
        mysql_free_result(clear_results);

        // Execute env_setting_stmts synchronously before the main query
        if (env_setting_stmts) {
            for (auto& s : *env_setting_stmts) {
                if (mysql_real_query(&mysql, s.c_str(), s.size())) {
                    cerr << "env_setting_stmt failed: " << mysql_error(&mysql) << endl;
                }
            }
        }

        status = mysql_real_query_nonblocking(&mysql, stmt.c_str(), stmt.size());
        sent_sql = stmt;
        has_sent_sql = true;
    }

    if (sent_sql != stmt) 
        throw std::runtime_error("sent sql stmt changed in " + debug_info + 
            "\nsent_sql: " + sent_sql +
            "\nstmt: " + stmt); 

    auto begin_time = get_cur_time_ms();
    while (1) {
        status = mysql_real_query_nonblocking(&mysql, stmt.c_str(), stmt.size());
        if (status != NET_ASYNC_NOT_READY)
            break;
            
        auto cur_time = get_cur_time_ms();
        if (cur_time - begin_time >= MYSQL_STMT_BLOCK_MS) {
            vector<unsigned long> blocking_tids;
            auto blocked = check_whether_block(blocking_tids);
            if (blocked == true) {
                string err_str = "blocked in " + debug_info;
                for (auto blocking_tid:blocking_tids)
                    err_str += "\nblocking_pid->[" + to_string(blocking_tid) + "]<-";
                err_str += "\nself_pid->[" + get_process_id() + "]<-";
                throw runtime_error(err_str); 
            }
                
            begin_time = cur_time;
        }
    }

    if (status == NET_ASYNC_ERROR) {
        string err = mysql_error(&mysql);
        has_sent_sql = false;
        sent_sql = "";
        auto result = mysql_store_result(&mysql);
        mysql_free_result(result);

        if (err.find("Commands out of sync") != string::npos) {// occasionally happens, retry the statement again
            cerr << err << ", repeat the statement again" << endl;
            test(stmt, output, affected_row_num);
            return;
        }
        if (err.find("Deadlock found") != string::npos) 
            txn_abort = true;
        throw std::runtime_error("NET_ASYNC_ERROR(skipped): " + err + "\nLocation: " + debug_info); 
    }

    if (affected_row_num)
        *affected_row_num = mysql_affected_rows(&mysql);

    auto result = mysql_store_result(&mysql);
    if (mysql_errno(&mysql) != 0) {
        string err = mysql_error(&mysql);
        has_sent_sql = false;
        sent_sql = "";
        mysql_free_result(result);
        if (err.find("Deadlock found") != string::npos) 
            txn_abort = true;
        throw std::runtime_error("mysql_store_result fails, stmt skipped: " + err + "\nLocation: " + debug_info); 
    }

    if (output && result) {
        auto row_num = mysql_num_rows(result);
        if (row_num == 0) {
            mysql_free_result(result);
            has_sent_sql = false;
            sent_sql = "";
            return;
        }

        auto column_num = mysql_num_fields(result);
        while (auto row = mysql_fetch_row(result)) {
            vector<string> row_output;
            for (int i = 0; i < column_num; i++) {
                string str;
                if (row[i] == NULL)
                    str = "NULL";
                else
                    str = row[i];
                row_output.push_back(str);
            }
            output->push_back(row_output);
        }
    }
    mysql_free_result(result);

    has_sent_sql = false;
    sent_sql = "";
    return;
}
#else
// Blocking fallback for environments without mysql_real_query_nonblocking
void dut_mysql::test(const string &stmt,
    vector<vector<string>>* output,
    int* affected_row_num,
    vector<string>* env_setting_stmts)
{
    if (env_setting_stmts) {
        for (auto& s : *env_setting_stmts) {
            block_test(s);
        }
    }

    if (mysql_real_query(&mysql, stmt.c_str(), stmt.size())) {
        string err = mysql_error(&mysql);
        auto result = mysql_store_result(&mysql);
        mysql_free_result(result);
        if (err.find("Commands out of sync") != string::npos) {
            cerr << err << " in test, repeat the statement again" << endl;
            test(stmt, output, affected_row_num, env_setting_stmts);
            return;
        }
        if (regex_match(err, e_crash)) {
            throw std::runtime_error("BUG!!! " + err + " in mysql::test");
        }
        string prefix = "mysql test expected error:";
        if (regex_match(err, e_dup_entry)
            || regex_match(err, e_large_results)
            || regex_match(err, e_timeout)
            || regex_match(err, e_col_ambiguous)
            || regex_match(err, e_truncated)
            || regex_match(err, e_division_zero)
            || regex_match(err, e_unknown_col)
            || regex_match(err, e_incorrect_args)
            || regex_match(err, e_out_of_range)
            || regex_match(err, e_win_context)
            || regex_match(err, e_view_reference)
            || regex_match(err, e_context_cancel)
            || regex_match(err, e_string_convert)
            || regex_match(err, e_col_null)
            || regex_match(err, e_sridb_pk)
            || regex_match(err, e_syntax)
            || regex_match(err, e_invalid_group)
            || regex_match(err, e_invalid_group_2)
            || regex_match(err, e_oom)
            || regex_match(err, e_schema_changed)
            || regex_match(err, e_over_mem)
            || regex_match(err, e_no_default)
            || regex_match(err, e_no_group_by)
            || regex_match(err, e_no_support_1)
            || regex_match(err, e_no_support_2)
            || regex_match(err, e_invalid_arguement)
            || regex_match(err, e_incorrect_string)
            || regex_match(err, e_long_specified_key)
            || regex_match(err, e_out_of_range_2)
            || regex_match(err, e_table_not_exists)
            || regex_match(err, e_ref_table_not_exists)
            || regex_match(err, e_subquery_transform)
            || regex_match(err, e_fk_constraint_fail)
            || regex_match(err, e_fk_missing_key)
           ) {
            throw runtime_error(prefix + err);
        }
        throw std::runtime_error("[" + err + "] in mysql::test");
    }

    if (affected_row_num)
        *affected_row_num = mysql_affected_rows(&mysql);

    auto result = mysql_store_result(&mysql);
    if (mysql_errno(&mysql) != 0) {
        string err = mysql_error(&mysql);
        if (regex_match(err, e_out_of_range)
            || regex_match(err, e_string_convert)
            || regex_match(err, e_table_not_exists)
            ) {
            throw runtime_error("mysql test/mysql_store_result expected error: " + err);
        }
        throw std::runtime_error("test: mysql_store_result fails [" + err + "]\nLocation: " + debug_info);
    }

    if (output && result) {
        auto row_num = mysql_num_rows(result);
        if (row_num == 0) {
            mysql_free_result(result);
            return;
        }

        auto column_num = mysql_num_fields(result);
        while (auto row = mysql_fetch_row(result)) {
            vector<string> row_output;
            for (int i = 0; i < column_num; i++) {
                string str;
                if (row[i] == NULL)
                    str = "NULL";
                else
                    str = row[i];
                row_output.push_back(process_an_item(str));
            }
            output->push_back(row_output);
        }
    }
    mysql_free_result(result);
    return;
}
#endif

void dut_mysql::reset(void)
{
    string drop_sql = "drop database if exists " + test_db + "; ";
    if (mysql_real_query(&mysql, drop_sql.c_str(), drop_sql.size())) {
        string err = mysql_error(&mysql);
        throw std::runtime_error(err + "\nLocation: " + debug_info);
    }
    auto res_sql = mysql_store_result(&mysql);
    mysql_free_result(res_sql);

    string create_sql = "create database " + test_db + "; ";
    if (mysql_real_query(&mysql, create_sql.c_str(), create_sql.size())) {
        string err = mysql_error(&mysql);
        throw std::runtime_error(err + "\nLocation: " + debug_info);
    }
    res_sql = mysql_store_result(&mysql);
    mysql_free_result(res_sql);

    string use_sql = "use " + test_db + "; ";
    if (mysql_real_query(&mysql, use_sql.c_str(), use_sql.size())) {
        string err = mysql_error(&mysql);
        throw std::runtime_error(err + "\nLocation: " + debug_info);
    }
    res_sql = mysql_store_result(&mysql);
    mysql_free_result(res_sql);
}

void dut_mysql::backup(void)
{
    auto backup_name = "/tmp/" + test_db + "_bk.sql";
    string pass_opt = test_pass.empty() ? "" : " -p" + test_pass;
    string mysql_dump = "mysqldump -h " + test_host + " -P " + to_string(test_port) + " -u " + test_user + pass_opt + " " + test_db + " > " + backup_name;
    int ret = system(mysql_dump.c_str());
    if (ret != 0) {
        cerr << "backup fail in dut_mysql::backup!!" << endl;
        throw std::runtime_error("backup fail in dut_mysql::backup"); 
    }
}

void dut_mysql::reset_to_backup(void)
{
    reset();
    string bk_file = "/tmp/" + test_db + "_bk.sql";
    if (access(bk_file.c_str(), F_OK ) == -1) 
        return;
    
    mysql_close(&mysql);

    string pass_opt = test_pass.empty() ? "" : " -p" + test_pass;
    string mysql_source = "mysql -h " + test_host + " -P " + to_string(test_port) + " -u " + test_user + pass_opt + " -D " + test_db + " < " + bk_file;
    if (system(mysql_source.c_str()) == -1)
        throw std::runtime_error(string("system() error, return -1") + " in dut_mysql::reset_to_backup!");

    const char* pass_ptr = test_pass.empty() ? NULL : test_pass.c_str();
    if (!mysql_real_connect(&mysql, test_host.c_str(), test_user.c_str(), pass_ptr, test_db.c_str(), test_port, NULL, 0))
        throw std::runtime_error(string(mysql_error(&mysql)) + " in dut_mysql::reset_to_backup!");
}

int dut_mysql::save_backup_file(string testdb, string path)
{
    string bk_file = "/tmp/" + testdb + "_bk.sql";
    string cp_cmd = "cp " + bk_file + " " + path;
    return system(cp_cmd.c_str());
}

int dut_mysql::use_backup_file(string backup_file)
{
    string cp_cmd = "cp " + backup_file + " /tmp/mysql_bk.sql";
    return system(cp_cmd.c_str());
}

void dut_mysql::get_content(vector<string>& tables_name, map<string, vector<vector<string>>>& content)
{
    for (auto& table:tables_name) {
        vector<vector<string>> table_content;
        auto query = "SELECT * FROM " + table + " ORDER BY 1;";

        if (mysql_real_query(&mysql, query.c_str(), query.size())) {
            string err = mysql_error(&mysql);
            cerr << "Cannot get content of " + table + "\nLocation: " + debug_info << endl;
            cerr << "Error: " + err + "\nLocation: " + debug_info << endl;
            continue;
        }

        auto result = mysql_store_result(&mysql);
        if (result) {
            auto column_num = mysql_num_fields(result);
            while (auto row = mysql_fetch_row(result)) {
                vector<string> row_output;
                for (int i = 0; i < column_num; i++) {
                    string str;
                    if (row[i] == NULL)
                        str = "NULL";
                    else
                        str = row[i];
                    row_output.push_back(process_an_item(str));
                }
                table_content.push_back(row_output);
            }
        }
        mysql_free_result(result);

        content[table] = table_content;
    }
}

#ifdef HAVE_MYSQL_NONBLOCK
string dut_mysql::get_process_id() {
    return to_string(thread_id);
}
#endif

string dut_mysql::begin_stmt() {
    return "START TRANSACTION";
}

string dut_mysql::commit_stmt() {
    return "COMMIT";
}

string dut_mysql::abort_stmt() {
    return "ROLLBACK";
}

#ifdef HAVE_MYSQL_NONBLOCK
pid_t dut_mysql::fork_db_server()
{
    pid_t child = fork();
    if (child < 0) {
        throw std::runtime_error(string("Fork db server fail") + "\nLocation: " + debug_info);
    }

    if (child == 0) {
        char *server_argv[128];
        int i = 0;
        server_argv[i++] = (char *)"/usr/local/mysql/bin/mysqld"; // path of tiup
        server_argv[i++] = (char *)"--basedir=/usr/local/mysql";
        server_argv[i++] = (char *)"--datadir=/usr/local/mysql/data";
        server_argv[i++] = (char *)"--plugin-dir=/usr/local/mysql/lib/plugin";
        server_argv[i++] = (char *)"--user=mysql";
        server_argv[i++] = NULL;
        execv(server_argv[0], server_argv);
        cerr << "fork mysql server fail \nLocation: " + debug_info << endl;
    }

    sleep(3);
    cout << "server pid: " << child << endl;
    return child;
}
#endif
