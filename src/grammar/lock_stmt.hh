/// @file
/// @brief MySQL LOCK TABLES / UNLOCK TABLES

#ifndef LOCK_STMT_HH
#define LOCK_STMT_HH

#include "core/prod.hh"
#include "core/relmodel.hh"
#include "schema/schema.hh"

struct lock_stmt : prod {
    bool is_lock;      // true = LOCK TABLES, false = UNLOCK TABLES
    string table_name;
    string lock_type;  // READ, WRITE, READ LOCAL, WRITE LOCAL

    lock_stmt(prod *p, struct scope *s);
    virtual void out(std::ostream &out);
    virtual void accept(prod_visitor *v) { v->visit(this); }
};

#endif
