/// @file
/// @brief MySQL SAVEPOINT / RELEASE SAVEPOINT / ROLLBACK TO SAVEPOINT

#ifndef SAVEPOINT_STMT_HH
#define SAVEPOINT_STMT_HH

#include "core/prod.hh"
#include "core/relmodel.hh"
#include "schema/schema.hh"

struct savepoint_stmt : prod {
    enum sp_type { SAVEPOINT, RELEASE_SAVEPOINT, ROLLBACK_TO };
    sp_type type;
    string sp_name;

    savepoint_stmt(prod *p, struct scope *s);
    virtual void out(std::ostream &out);
    virtual void accept(prod_visitor *v) { v->visit(this); }
};

#endif
