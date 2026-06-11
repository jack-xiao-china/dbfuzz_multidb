#pragma once

#include "expr/value_expr.hh"

/// @brief CAST / CONVERT expression for MySQL and PostgreSQL
/// Generates: CAST(expr AS type) or CONVERT(expr, type)
struct cast_expr : value_expr {
    shared_ptr<value_expr> inner_expr;
    string target_type_name;  // SQL output: "SIGNED", "CHAR", "DECIMAL", "DATE", "DATETIME", "TIME", "BINARY"
    sqltype *target_type;     // corresponding sqltype pointer
    bool use_convert;         // true = CONVERT(expr, type), false = CAST(expr AS type)

    cast_expr(prod *p, sqltype *type_constraint = 0);
    virtual void out(std::ostream &out);
    virtual void accept(prod_visitor *v);
    virtual void equivalent_transform();
    virtual void back_transform();
    virtual void set_component_id(int &id);
    virtual bool get_component_from_id(int id, shared_ptr<value_expr> &component);
    virtual bool set_component_from_id(int id, shared_ptr<value_expr> component);
};
