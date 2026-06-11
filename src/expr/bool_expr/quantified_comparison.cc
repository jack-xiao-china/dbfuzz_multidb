#include "expr/bool_expr/quantified_comparison.hh"
#include "expr/value_expr.hh"
#include "expr/column_reference.hh"
#include "expr/const_expr.hh"
#include "grammar/grammar.hh"

quantified_comparison::quantified_comparison(prod *p, struct scope *s)
    : bool_expr(p)
{
    match();
    scope = &myscope;
    myscope.parent = s;
    myscope.schema = s->schema;
    myscope.tables = s->tables;
    myscope.refs = s->refs;
    myscope.stmt_seq = s->stmt_seq;
    myscope.indexes = s->indexes;

    if (!scope->schema->features.has_quantified_cmp)
        fail("quantified comparison not supported by this DBMS");

    // Select comparison operator
    static const char* ops[] = {"=", "<>", "<", ">", "<=", ">="};
    comp_op = ops[smith::rng() % 6];

    // Select quantifier
    switch (d6()) {
        case 1: case 2:
            quant = ALL; break;
        case 3: case 4:
            quant = ANY; break;
        default:
            quant = SOME; break;
    }

    // Pick a column for the left-hand side (prefer int type)
    auto int_refs = scope->refs_of_type(scope->schema->inttype);
    if (!int_refs.empty()) {
        auto &ref = int_refs[smith::rng() % int_refs.size()];
        lhs = make_shared<column_reference>(this, ref.second.type, ref.second.name, ref.first->name);
    } else {
        // Fallback: use any available column reference
        if (!scope->refs.empty()) {
            lhs = make_shared<column_reference>(this);
        } else {
            lhs = make_shared<const_expr>(this, scope->schema->inttype);
        }
    }

    // Generate subquery that returns a single column of compatible type
    vector<sqltype*> pointed_type = {lhs->type};
    subquery = make_shared<query_spec>(this, &myscope, false,
                                        &pointed_type, false);
}

void quantified_comparison::out(std::ostream &out)
{
    out << "(" << *lhs << ") " << comp_op << " ";
    switch (quant) {
        case ALL:  out << "ALL "; break;
        case ANY:  out << "ANY "; break;
        case SOME: out << "SOME "; break;
    }
    out << "(" << *subquery << ")";
}

void quantified_comparison::accept(prod_visitor *v)
{
    v->visit(this);
    lhs->accept(v);
    subquery->accept(v);
}
