#pragma once

#include "expr/bool_expr/bool_binop/bool_binop.hh"

struct distinct_pred : bool_binop
{
    distinct_pred(prod *p);
    virtual ~distinct_pred(){};
    virtual void out(ostream &o);
};