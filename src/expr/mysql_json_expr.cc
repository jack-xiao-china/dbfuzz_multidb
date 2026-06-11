#include "expr/mysql_json_expr.hh"

mysql_json_op::mysql_json_op(prod *p, sqltype *type_constraint)
    : value_expr(p)
{
    if (!scope->schema->features.has_mysql_json)
        fail("MySQL JSON operators not supported by this DBMS");

    // Pick operator type
    if (d6() > 3) {
        op = JSON_DOUBLE_ARROW;  // ->> returns text (unquoted)
        type = scope->schema->texttype;
    } else {
        op = JSON_ARROW;  // -> returns JSON
        type = scope->schema->texttype;  // use texttype since we don't have jsontype as canonical
    }

    // If type_constraint doesn't match, fail and let factory retry
    if (type_constraint && !type_constraint->consistent(type))
        fail("MySQL JSON operator type mismatch");

    // Generate LHS expression (prefer text/json columns)
    lhs = value_expr::factory(this, scope->schema->texttype);

    // Generate a random JSON path
    static const char *paths[] = {
        "'$.key'", "'$.name'", "'$.value'", "'$.data'",
        "'$[0]'", "'$[1]'", "'$[2]'",
        "'$.a.b'", "'$.x.y.z'",
        "'$.items[0]'"
    };
    json_path = paths[smith::rng() % 10];
}

void mysql_json_op::out(std::ostream &out)
{
    if (is_transformed && !has_print_eq_expr) {
        out_eq_value_expr(out);
        return;
    }

    out << "(" << *lhs << ")";
    switch (op) {
        case JSON_ARROW:        out << "->"; break;
        case JSON_DOUBLE_ARROW: out << "->>"; break;
    }
    out << json_path;
}

void mysql_json_op::equivalent_transform()
{
    value_expr::equivalent_transform();
    lhs->equivalent_transform();
}

void mysql_json_op::back_transform()
{
    lhs->back_transform();
    value_expr::back_transform();
}

void mysql_json_op::set_component_id(int &id)
{
    value_expr::set_component_id(id);
    lhs->set_component_id(id);
}

bool mysql_json_op::get_component_from_id(int id, shared_ptr<value_expr> &component)
{
    GET_COMPONENT_FROM_ID_CHILD(id, component, lhs);
    return value_expr::get_component_from_id(id, component);
}

bool mysql_json_op::set_component_from_id(int id, shared_ptr<value_expr> component)
{
    SET_COMPONENT_FROM_ID_CHILD(id, component, lhs);
    return value_expr::set_component_from_id(id, component);
}
