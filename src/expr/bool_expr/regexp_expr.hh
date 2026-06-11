/// @file
/// @brief MySQL-specific boolean expressions: REGEXP, SOUNDS LIKE, MEMBER OF

#ifndef REGEXP_EXPR_HH
#define REGEXP_EXPR_HH

#include "core/prod.hh"
#include "core/relmodel.hh"
#include "expr/value_expr.hh"
#include "expr/bool_expr/bool_expr.hh"

/// MySQL REGEXP / RLIKE / NOT REGEXP (sql_yacc.yy predicate rule)
struct regexp_expr : bool_expr {
    shared_ptr<value_expr> lhs;
    shared_ptr<value_expr> rhs;
    bool negated;
    regexp_expr(prod *p, struct scope *s);
    virtual ~regexp_expr() { }
    virtual void out(std::ostream &out);
    virtual void accept(prod_visitor *v) {
        v->visit(this);
        lhs->accept(v);
        rhs->accept(v);
    }
};

/// MySQL SOUNDS LIKE (sql_yacc.yy predicate rule)
struct sounds_like_expr : bool_expr {
    shared_ptr<value_expr> lhs;
    shared_ptr<value_expr> rhs;
    sounds_like_expr(prod *p, struct scope *s);
    virtual ~sounds_like_expr() { }
    virtual void out(std::ostream &out);
    virtual void accept(prod_visitor *v) {
        v->visit(this);
        lhs->accept(v);
        rhs->accept(v);
    }
};

#endif
