#pragma once

#include "expr/bool_expr/bool_expr.hh"

// Forward declaration
struct query_spec;

/// Quantified comparison: expr {=|<>|<|>|<=|>=} {ALL|ANY|SOME} (subquery)
struct quantified_comparison : bool_expr {
    enum quantifier { ALL, ANY, SOME };
    string comp_op;
    quantifier quant;
    shared_ptr<value_expr> lhs;
    shared_ptr<query_spec> subquery;
    struct scope myscope;

    quantified_comparison(prod *p, struct scope *s);
    virtual void out(std::ostream &out);
    virtual ~quantified_comparison() {}
    virtual void accept(prod_visitor *v);
};
