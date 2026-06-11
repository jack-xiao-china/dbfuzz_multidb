#pragma once

#include "expr/value_expr.hh"

/// @brief INTERVAL date arithmetic expression for MySQL
/// Generates: expr + INTERVAL n DAY  or  expr - INTERVAL n MONTH  etc.
struct interval_expr : value_expr {
    shared_ptr<value_expr> date_expr;
    shared_ptr<value_expr> amount;
    string interval_unit;  // DAY, MONTH, YEAR, HOUR, MINUTE, SECOND
    bool is_add;           // true = date + INTERVAL, false = date - INTERVAL

    interval_expr(prod *p, sqltype *type_constraint = 0);
    virtual void out(std::ostream &out);
    virtual void accept(prod_visitor *v);
    virtual void equivalent_transform();
    virtual void back_transform();
    virtual void set_component_id(int &id);
    virtual bool get_component_from_id(int id, shared_ptr<value_expr> &component);
    virtual bool set_component_from_id(int id, shared_ptr<value_expr> component);
};
