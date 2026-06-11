#include "expr/bool_expr/member_of_expr.hh"

member_of_expr::member_of_expr(prod *p)
    : bool_expr(p)
{
    if (!scope->schema->features.has_mysql_json)
        fail("MEMBER OF not supported by this DBMS");

    // Generate a value expression
    value = value_expr::factory(this);

    // Generate a JSON array expression (use texttype as proxy for JSON)
    json_array = value_expr::factory(this, scope->schema->texttype);
}

void member_of_expr::out(std::ostream &out)
{
    OUTPUT_EQ_BOOL_EXPR(out);

    out << "(" << *value << " member of(" << *json_array << "))";
}
