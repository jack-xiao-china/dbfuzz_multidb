#include "grammar/json_table.hh"
#include "core/random.hh"

using namespace std;

json_table_ref::json_table_ref(prod *p) : table_ref(p)
{
    // Step 1: Find a table with a JSON column in scope
    vector<pair<named_relation*, column*>> json_candidates;
    for (auto t : scope->tables) {
        for (auto &c : t->columns()) {
            if (c.type->name == "json" || c.type->name == "JSON") {
                json_candidates.push_back({t, const_cast<column*>(&c)});
            }
        }
    }
    if (json_candidates.empty())
        throw runtime_error("json_table_ref: no JSON columns available in scope");

    auto picked = random_pick(json_candidates);
    source_table = picked.first;
    json_col = picked.second;

    // Step 2: Generate 2-4 output columns
    static int jt_counter = 0;
    jt_counter++;
    alias = "jt_" + to_string(jt_counter);

    int num_cols = 2 + (d6() % 3);  // 2-4 columns
    static vector<string> paths = {
        "$.id", "$.name", "$.value", "$.status", "$.count",
        "$.price", "$.date", "$.type", "$.code", "$.label"
    };

    vector<string> col_names;
    for (int i = 0; i < num_cols; i++) {
        json_column_def jcd;
        jcd.name = "jc_" + to_string(i);

        // Pick type
        static vector<pair<string, string>> type_paths = {
            {"int", "$.id"}, {"int", "$.count"}, {"int", "$.value"},
            {"varchar(50)", "$.name"}, {"varchar(50)", "$.label"}, {"varchar(50)", "$.status"},
            {"double", "$.price"}, {"double", "$.value"}
        };
        auto tp = random_pick(type_paths);
        jcd.type_name = tp.first;
        jcd.json_path = tp.second;

        column_defs.push_back(jcd);
        col_names.push_back(jcd.name);
    }

    // Step 3: Create named_relation for this table reference
    auto rel = make_shared<named_relation>(alias);
    refs.push_back(rel);
}

void json_table_ref::out(std::ostream &out)
{
    out << "json_table("
        << source_table->ident() << "." << json_col->name
        << ", '$[*]' columns (";

    for (size_t i = 0; i < column_defs.size(); i++) {
        auto &cd = column_defs[i];
        out << cd.name << " " << cd.type_name
            << " path '" << cd.json_path << "'";
        if (i + 1 < column_defs.size())
            out << ", ";
    }
    out << ")) as " << alias;
}
