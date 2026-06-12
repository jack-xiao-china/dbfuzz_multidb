#include "expr/bool_expr/correlated_subquery.hh"
#include "core/random.hh"

using namespace std;

correlated_subquery::correlated_subquery(prod *p)
    : bool_expr(p), outer_ref(nullptr), outer_col(nullptr),
      inner_tab(nullptr), inner_col(nullptr), agg_func(nullptr), use_exists(false)
{
    // Step 1: Pick an outer table and column from scope->refs
    if (scope->refs.empty())
        throw runtime_error("correlated_subquery: no outer refs available");

    outer_ref = random_pick(scope->refs);
    if (outer_ref->columns().empty())
        throw runtime_error("correlated_subquery: outer table has no columns");
    outer_col = &random_pick(outer_ref->columns());

    // Step 2: Pick an inner table from scope->tables (different from outer)
    vector<named_relation*> inner_candidates;
    for (auto t : scope->tables) {
        if (t != outer_ref && !t->columns().empty()) {
            // Check if it has a column of matching type
            for (auto &c : t->columns()) {
                if (c.type == outer_col->type) {
                    inner_candidates.push_back(t);
                    break;
                }
            }
        }
    }
    if (inner_candidates.empty())
        throw runtime_error("correlated_subquery: no matching inner table");

    inner_tab = random_pick(inner_candidates);

    // Find the matching column in inner table
    for (auto &c : inner_tab->columns()) {
        if (c.type == outer_col->type) {
            inner_col = &c;
            break;
        }
    }

    // Step 3: Choose comparison operator
    static vector<string> ops = {"=", "<>", ">", "<", ">=", "<="};
    compare_op = random_pick(ops);

    // Step 4: Optionally use aggregate or EXISTS
    if (d6() <= 2) {
        use_exists = true;
    } else if (d6() <= 3) {
        // Use aggregate function (COUNT, MAX, MIN, etc.)
        auto &aggs = scope->schema->aggregates;
        for (auto &a : aggs) {
            if (a.argtypes.size() == 1 && a.argtypes[0] == inner_col->type) {
                agg_func = &a;
                break;
            }
        }
    }
}

void correlated_subquery::out(std::ostream &out)
{
    if (use_exists) {
        out << "exists (select 1 from " << inner_tab->ident()
            << " where " << inner_tab->ident() << "." << inner_col->name
            << " " << compare_op << " "
            << outer_ref->ident() << "." << outer_col->name << ")";
    } else if (agg_func) {
        out << "(select " << agg_func->ident() << "("
            << inner_tab->ident() << "." << inner_col->name
            << ") from " << inner_tab->ident() << ") "
            << compare_op << " "
            << outer_ref->ident() << "." << outer_col->name;
    } else {
        out << outer_ref->ident() << "." << outer_col->name
            << " " << compare_op
            << " (select " << inner_tab->ident() << "." << inner_col->name
            << " from " << inner_tab->ident()
            << " order by " << inner_tab->ident() << "." << inner_col->name
            << " limit 1 offset " << d6() << ")";
    }
}
