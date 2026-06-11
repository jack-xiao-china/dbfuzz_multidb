#include "expr/json_expr.hh"
#include "expr/column_reference.hh"
#include "expr/const_expr.hh"

// ── json_extract_op ──

json_extract_op::json_extract_op(prod *p, sqltype *type_constraint)
    : value_expr(p)
{
    match();

    if (!scope->schema->features.has_json_jsonb)
        fail("JSON operators not supported by this DBMS");

    // Select operator type
    switch (d6()) {
        case 1: case 2:
            op = JSON_ARROW;        // -> returns jsonb
            type = scope->schema->texttype;  // simplified: treat as text
            break;
        case 3: case 4:
            op = JSON_DOUBLE_ARROW; // ->> returns text
            type = scope->schema->texttype;
            break;
        case 5:
            op = JSON_HASH_ARROW;   // #> returns jsonb
            type = scope->schema->texttype;
            break;
        default:
            op = JSON_HASH_DOUBLE;  // #>> returns text
            type = scope->schema->texttype;
            break;
    }

    // LHS: pick a column (ideally jsonb type, but fall back to any column with cast)
    if (!scope->refs.empty()) {
        lhs = make_shared<column_reference>(this);
    } else {
        lhs = make_shared<const_expr>(this, scope->schema->texttype);
    }

    // Generate key or path
    if (op == JSON_HASH_ARROW || op == JSON_HASH_DOUBLE) {
        // Path syntax: {key1,key2,...}
        key_or_path = "'{" + to_string(d100() % 5) + "}'";
    } else {
        // Simple key
        key_or_path = "'" + to_string(d100() % 10) + "'";
    }
}

void json_extract_op::out(std::ostream &out)
{
    out << "(" << *lhs << ")";
    switch (op) {
        case JSON_ARROW:        out << "->"; break;
        case JSON_DOUBLE_ARROW: out << "->>"; break;
        case JSON_HASH_ARROW:   out << "#>"; break;
        case JSON_HASH_DOUBLE:  out << "#>>"; break;
    }
    out << key_or_path;
}

void json_extract_op::accept(prod_visitor *v)
{
    v->visit(this);
    lhs->accept(v);
}

// ── array_constructor ──

array_constructor::array_constructor(prod *p, sqltype *type_constraint)
    : value_expr(p)
{
    match();

    if (!scope->schema->features.has_array_ops)
        fail("array operations not supported by this DBMS");

    // Use int type as default element type
    element_type = scope->schema->inttype;
    type = element_type;  // simplified type

    // Generate 1-5 elements
    int num_elements = d6() > 4 ? (d6() > 5 ? 5 : 4) : (d6() > 2 ? 3 : 2);
    for (int i = 0; i < num_elements; i++) {
        elements.push_back(value_expr::factory(this, element_type));
    }
}

void array_constructor::out(std::ostream &out)
{
    out << "ARRAY[";
    for (size_t i = 0; i < elements.size(); i++) {
        out << *elements[i];
        if (i + 1 < elements.size()) out << ", ";
    }
    out << "]";
}

void array_constructor::accept(prod_visitor *v)
{
    v->visit(this);
    for (auto &e : elements)
        e->accept(v);
}
