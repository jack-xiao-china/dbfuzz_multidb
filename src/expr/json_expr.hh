#pragma once

#include "expr/value_expr.hh"

/// PostgreSQL JSON extract operators: ->, ->>, #>, #>>
struct json_extract_op : value_expr {
    enum op_type { JSON_ARROW, JSON_DOUBLE_ARROW, JSON_HASH_ARROW, JSON_HASH_DOUBLE };
    op_type op;
    shared_ptr<value_expr> lhs;
    string key_or_path;

    json_extract_op(prod *p, sqltype *type_constraint);
    virtual void out(std::ostream &out);
    virtual ~json_extract_op() {}
    virtual void accept(prod_visitor *v);
};

/// PostgreSQL ARRAY constructor: ARRAY[expr1, expr2, ...]
struct array_constructor : value_expr {
    vector<shared_ptr<value_expr>> elements;
    sqltype *element_type;

    array_constructor(prod *p, sqltype *type_constraint);
    virtual void out(std::ostream &out);
    virtual ~array_constructor() {}
    virtual void accept(prod_visitor *v);
};
