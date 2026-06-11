#include "expr/window_function.hh"
#include "grammar/grammar.hh"

// ── window_frame implementation ──

window_frame::window_frame(prod *p) : prod(p)
{
    // Select frame mode
    switch (d6()) {
        case 1: case 2: case 3:
            mode = ROWS_MODE; break;
        case 4: case 5:
            mode = RANGE_MODE; break;
        default:
            mode = GROUPS_MODE; break;
    }

    // Start boundary: UNBOUNDED_PRECEDING / N_PRECEDING / CURRENT_ROW
    switch (d6()) {
        case 1:
            start_bound = UNBOUNDED_PRECEDING;
            start_offset = 0;
            break;
        case 2: case 3:
            start_bound = N_PRECEDING;
            start_offset = d100() % 10 + 1;
            break;
        default:
            start_bound = CURRENT_ROW;
            start_offset = 0;
            break;
    }

    // End boundary: CURRENT_ROW / N_FOLLOWING / UNBOUNDED_FOLLOWING
    switch (d6()) {
        case 1:
            end_bound = CURRENT_ROW;
            end_offset = 0;
            break;
        case 2: case 3:
            end_bound = N_FOLLOWING;
            end_offset = d100() % 10 + 1;
            break;
        default:
            end_bound = UNBOUNDED_FOLLOWING;
            end_offset = 0;
            break;
    }
}

void window_frame::out(std::ostream &out)
{
    switch (mode) {
        case ROWS_MODE:   out << " rows"; break;
        case RANGE_MODE:  out << " range"; break;
        case GROUPS_MODE: out << " groups"; break;
    }

    // Shorthand: single CURRENT_ROW boundary
    if (start_bound == CURRENT_ROW && end_bound == CURRENT_ROW) {
        out << " current row";
        return;
    }

    out << " between ";

    switch (start_bound) {
        case UNBOUNDED_PRECEDING:
            out << "unbounded preceding"; break;
        case N_PRECEDING:
            out << start_offset << " preceding"; break;
        case CURRENT_ROW:
            out << "current row"; break;
        default: break;
    }

    out << " and ";

    switch (end_bound) {
        case CURRENT_ROW:
            out << "current row"; break;
        case N_FOLLOWING:
            out << end_offset << " following"; break;
        case UNBOUNDED_FOLLOWING:
            out << "unbounded following"; break;
        default: break;
    }
}

// ── window_function implementation ──

void window_function::out(ostream &out)
{
    if (is_transformed && !has_print_eq_expr)
    {
        out_eq_value_expr(out);
        return;
    }

    out << *aggregate << " over (partition by ";
    for (auto ref = partition_by.begin(); ref != partition_by.end(); ref++)
    {
        out << **ref;
        if (ref + 1 != partition_by.end())
            out << ",";
    }

    out << " order by ";
    for (auto ref = order_by.begin(); ref != order_by.end(); ref++)
    {
        auto &order_pair = *ref;
        out << *(order_pair.first) << " ";
        out << (order_pair.second ? "asc" : "desc");
        if (ref + 1 != order_by.end())
            out << ", ";
    }

    // Append frame clause if present
    if (frame)
        out << *frame;

    out << ")";
}

window_function::window_function(prod *p, sqltype *type_constraint)
    : value_expr(p)
{
    match();
    aggregate = make_shared<win_funcall>(this, type_constraint);

    type = aggregate->type;
    partition_by.push_back(make_shared<column_reference>(this));
    while (d6() > 4)
        partition_by.push_back(make_shared<column_reference>(this));

    // order by all possible col ref, make the result determined
    for (auto r : scope->refs)
    {
        for (auto &c : (*r).columns())
        {
            auto col = make_shared<column_reference>(this, c.type, c.name, r->name);
            auto is_asc = d6() <= 3 ? true : false;
            order_by.push_back(make_pair<>(col, is_asc));
        }
    }

    // Generate frame clause with 50% probability (when DBMS supports it)
    if (d6() > 3 && scope->schema->features.has_window_frame)
        frame = make_shared<window_frame>(this);
}

bool window_function::disabled = false;
bool window_function::allowed(prod *p)
{
    if (disabled)
        return false;
    if (dynamic_cast<select_list *>(p))
        return dynamic_cast<query_spec *>(p->pprod) ? true : false;
    if (dynamic_cast<window_function *>(p))
        return false;
    if (dynamic_cast<value_expr *>(p))
        return allowed(p->pprod);
    return false;
}

void window_function::accept(prod_visitor *v)
{
    v->visit(this);
    aggregate->accept(v);
    for (auto p : partition_by)
        p->accept(v);
    for (auto p : order_by)
        p.first->accept(v);
    if (frame)
        frame->accept(v);
}