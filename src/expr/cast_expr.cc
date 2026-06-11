#include "expr/cast_expr.hh"

cast_expr::cast_expr(prod *p, sqltype *type_constraint)
    : value_expr(p)
{
    if (!scope->schema->features.has_cast)
        fail("CAST not supported by this DBMS");

    // Build list of available target types
    struct cast_target {
        const char *sql_name;
        sqltype *stype;
    };

    vector<cast_target> targets;
    targets.push_back({"SIGNED", scope->schema->inttype});
    targets.push_back({"CHAR", scope->schema->texttype});
    targets.push_back({"DECIMAL", scope->schema->realtype});
    targets.push_back({"DATE", scope->schema->datetype});
    targets.push_back({"DATETIME", scope->schema->datetype});
    targets.push_back({"TIME", scope->schema->datetype});

    // If type_constraint is specified, prefer matching targets
    if (type_constraint) {
        vector<cast_target> matching;
        for (auto &t : targets) {
            if (t.stype == type_constraint || type_constraint->consistent(t.stype))
                matching.push_back(t);
        }
        if (!matching.empty()) {
            auto &chosen = random_pick(matching);
            target_type_name = chosen.sql_name;
            target_type = chosen.stype;
        } else {
            // No matching cast target for this type — fail so factory retries
            fail("no CAST target available for requested type");
        }
    } else {
        auto &chosen = random_pick(targets);
        target_type_name = chosen.sql_name;
        target_type = chosen.stype;
    }

    type = target_type;

    // Generate inner expression (any type)
    inner_expr = value_expr::factory(this);

    // MySQL: 50% chance to use CONVERT instead of CAST
    use_convert = (schema::target_dbms == "mysql" && d6() > 3);
}

void cast_expr::out(std::ostream &out)
{
    if (is_transformed && !has_print_eq_expr) {
        out_eq_value_expr(out);
        return;
    }

    if (use_convert) {
        // MySQL CONVERT(expr, type) syntax
        out << "convert(" << *inner_expr << ", " << target_type_name << ")";
    } else {
        // Standard CAST(expr AS type) syntax
        out << "cast(" << *inner_expr << " as " << target_type_name << ")";
    }
}

void cast_expr::accept(prod_visitor *v)
{
    v->visit(this);
    inner_expr->accept(v);
}

void cast_expr::equivalent_transform()
{
    value_expr::equivalent_transform();
    inner_expr->equivalent_transform();
}

void cast_expr::back_transform()
{
    inner_expr->back_transform();
    value_expr::back_transform();
}

void cast_expr::set_component_id(int &id)
{
    value_expr::set_component_id(id);
    inner_expr->set_component_id(id);
}

bool cast_expr::get_component_from_id(int id, shared_ptr<value_expr> &component)
{
    GET_COMPONENT_FROM_ID_CHILD(id, component, inner_expr);
    return value_expr::get_component_from_id(id, component);
}

bool cast_expr::set_component_from_id(int id, shared_ptr<value_expr> component)
{
    SET_COMPONENT_FROM_ID_CHILD(id, component, inner_expr);
    return value_expr::set_component_from_id(id, component);
}
