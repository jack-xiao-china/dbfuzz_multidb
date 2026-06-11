#pragma once

#include "expr/value_expr.hh"

/// ROW constructor: ROW(expr1, expr2, ...)
struct row_constructor : value_expr {
    vector<shared_ptr<value_expr>> fields;

    row_constructor(prod *p, sqltype *type_constraint);
    virtual void out(std::ostream &out);
    virtual ~row_constructor() {}
    virtual void accept(prod_visitor *v);
};
