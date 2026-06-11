#pragma once

#include "expr/value_expr.hh"

struct column_reference : value_expr
{
    string table_ref;
    string reference;
    string collation;  // optional COLLATE suffix for MySQL
    column_reference(prod *p, sqltype *type_constraint = 0,
                     vector<shared_ptr<named_relation>> *prefer_refs = 0);
    column_reference(prod *p, sqltype *type,
                     string column_name, string table_name);

    virtual void out(ostream &o);
    virtual ~column_reference() {}
};