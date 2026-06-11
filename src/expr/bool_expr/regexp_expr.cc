#include "expr/bool_expr/regexp_expr.hh"
#include "expr/const_expr.hh"

regexp_expr::regexp_expr(prod *p, struct scope *s) : bool_expr(p)
{
    if (!scope->schema->features.has_regexp)
        fail("REGEXP not supported by this DBMS");
    scope = s;
    lhs = value_expr::factory(this, scope->schema->texttype);
    rhs = value_expr::factory(this, scope->schema->texttype);
    negated = d6() > 4;
}

void regexp_expr::out(std::ostream &out)
{
    out << *lhs;
    if (negated)
        out << " not regexp ";
    else
        out << " regexp ";
    out << *rhs;
}

sounds_like_expr::sounds_like_expr(prod *p, struct scope *s) : bool_expr(p)
{
    if (!scope->schema->features.has_sounds_like)
        fail("SOUNDS LIKE not supported by this DBMS");
    scope = s;
    lhs = value_expr::factory(this, scope->schema->texttype);
    rhs = value_expr::factory(this, scope->schema->texttype);
}

void sounds_like_expr::out(std::ostream &out)
{
    out << *lhs << " sounds like " << *rhs;
}
