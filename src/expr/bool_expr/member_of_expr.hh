/// @file
/// @brief MySQL MEMBER OF expression

#ifndef MEMBER_OF_EXPR_HH
#define MEMBER_OF_EXPR_HH

#include "core/prod.hh"
#include "core/relmodel.hh"
#include "expr/value_expr.hh"
#include "expr/bool_expr/bool_expr.hh"

/// MySQL MEMBER OF() — tests whether a value is a member of a JSON array
struct member_of_expr : bool_expr {
    shared_ptr<value_expr> value;
    shared_ptr<value_expr> json_array;

    member_of_expr(prod *p);
    virtual ~member_of_expr() { }
    virtual void out(std::ostream &out);
    virtual void accept(prod_visitor *v) {
        v->visit(this);
        value->accept(v);
        json_array->accept(v);
    }
};

#endif
