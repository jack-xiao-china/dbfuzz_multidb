#pragma once

#include "expr/expr.hh"
#include "expr/bool_expr/bool_expr.hh"
#include "core/relmodel.hh"

/// Correlated subquery: a boolean expression containing a subquery that
/// references columns from the outer query (scope->refs).
/// Example: EXISTS (SELECT 1 FROM t2 WHERE t2.col = outer_t.outer_col)
struct correlated_subquery : bool_expr {
    named_relation *outer_ref;    // Table from outer scope
    column *outer_col;            // Column from outer table
    named_relation *inner_tab;    // Table in subquery
    column *inner_col;            // Column in subquery (type-matched)
    string compare_op;            // =, >, <, >=, <=, <>
    routine *agg_func;            // Optional aggregate (COUNT, MAX, etc.)
    bool use_exists;              // Use EXISTS instead of scalar comparison

    correlated_subquery(prod *p);
    virtual ~correlated_subquery() {}
    virtual void out(std::ostream &out);
    virtual void accept(prod_visitor *v) { v->visit(this); }
};
