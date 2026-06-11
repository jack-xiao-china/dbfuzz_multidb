#include "expr/row_constructor.hh"
#include "expr/const_expr.hh"

row_constructor::row_constructor(prod *p, sqltype *type_constraint)
    : value_expr(p)
{
    match();
    type = type_constraint ? type_constraint : scope->schema->texttype;

    // Generate 2-4 fields
    int num_fields = d6() > 4 ? 4 : (d6() > 3 ? 3 : 2);
    for (int i = 0; i < num_fields; i++) {
        fields.push_back(value_expr::factory(this));
    }
}

void row_constructor::out(std::ostream &out)
{
    out << "ROW(";
    for (size_t i = 0; i < fields.size(); i++) {
        out << *fields[i];
        if (i + 1 < fields.size()) out << ", ";
    }
    out << ")";
}

void row_constructor::accept(prod_visitor *v)
{
    v->visit(this);
    for (auto &f : fields)
        f->accept(v);
}
