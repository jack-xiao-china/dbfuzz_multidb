#include "expr/interval_expr.hh"

interval_expr::interval_expr(prod *p, sqltype *type_constraint)
    : value_expr(p)
{
    if (!scope->schema->features.has_interval_expr)
        fail("INTERVAL expression not supported by this DBMS");

    // Result type is always a date/time type
    type = scope->schema->datetype;

    // Generate date expression (prefer datetype columns)
    date_expr = value_expr::factory(this, scope->schema->datetype);

    // Generate amount (integer)
    amount = value_expr::factory(this, scope->schema->inttype);

    // Pick interval unit
    static const char *units[] = {
        "DAY", "MONTH", "YEAR", "HOUR", "MINUTE", "SECOND"
    };
    interval_unit = units[smith::rng() % 6];

    // Add or subtract
    is_add = (d6() > 2);  // 67% add, 33% subtract
}

void interval_expr::out(std::ostream &out)
{
    if (is_transformed && !has_print_eq_expr) {
        out_eq_value_expr(out);
        return;
    }

    out << "(" << *date_expr << ") "
        << (is_add ? "+" : "-") << " interval ("
        << *amount << ") " << interval_unit;
}

void interval_expr::accept(prod_visitor *v)
{
    v->visit(this);
    date_expr->accept(v);
    amount->accept(v);
}

void interval_expr::equivalent_transform()
{
    value_expr::equivalent_transform();
    date_expr->equivalent_transform();
    amount->equivalent_transform();
}

void interval_expr::back_transform()
{
    date_expr->back_transform();
    amount->back_transform();
    value_expr::back_transform();
}

void interval_expr::set_component_id(int &id)
{
    value_expr::set_component_id(id);
    date_expr->set_component_id(id);
    amount->set_component_id(id);
}

bool interval_expr::get_component_from_id(int id, shared_ptr<value_expr> &component)
{
    GET_COMPONENT_FROM_ID_CHILD(id, component, date_expr);
    GET_COMPONENT_FROM_ID_CHILD(id, component, amount);
    return value_expr::get_component_from_id(id, component);
}

bool interval_expr::set_component_from_id(int id, shared_ptr<value_expr> component)
{
    SET_COMPONENT_FROM_ID_CHILD(id, component, date_expr);
    SET_COMPONENT_FROM_ID_CHILD(id, component, amount);
    return value_expr::set_component_from_id(id, component);
}
