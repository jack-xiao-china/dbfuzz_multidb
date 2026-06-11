/// @file
/// @brief MySQL JSON -> and ->> operators

#ifndef MYSQL_JSON_EXPR_HH
#define MYSQL_JSON_EXPR_HH

#include "expr/value_expr.hh"

/// MySQL JSON column->path and column->>path operators
struct mysql_json_op : value_expr {
    enum op_type { JSON_ARROW, JSON_DOUBLE_ARROW };
    op_type op;
    shared_ptr<value_expr> lhs;
    string json_path;  // e.g. '$.key', '$[0]', '$."name"'

    mysql_json_op(prod *p, sqltype *type_constraint = 0);
    virtual ~mysql_json_op() { }
    virtual void out(std::ostream &out);
    virtual void accept(prod_visitor *v) {
        v->visit(this);
        lhs->accept(v);
    }
    virtual void equivalent_transform();
    virtual void back_transform();
    virtual void set_component_id(int &id);
    virtual bool get_component_from_id(int id, shared_ptr<value_expr> &component);
    virtual bool set_component_from_id(int id, shared_ptr<value_expr> component);
};

#endif
