#pragma once

#include "grammar/grammar.hh"
#include "core/relmodel.hh"
#include "schema/schema.hh"

/// JSON_TABLE table function (MySQL 8.0.17+)
/// Converts JSON data into a relational table in the FROM clause.
/// Example:
///   JSON_TABLE(t.json_col, '$[*]' COLUMNS (
///     id INT PATH '$.id',
///     name VARCHAR(50) PATH '$.name'
///   )) AS jt
struct json_table_ref : table_ref {
    named_relation *source_table;    // Table containing JSON column
    column *json_col;                // JSON column
    string alias;                    // Table alias (jt_N)

    struct json_column_def {
        string name;
        string type_name;
        string json_path;
    };
    vector<json_column_def> column_defs;

    json_table_ref(prod *p);
    virtual ~json_table_ref() {}
    virtual void out(std::ostream &out);
};
