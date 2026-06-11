#include "grammar/savepoint_stmt.hh"
#include "core/random.hh"

savepoint_stmt::savepoint_stmt(prod *p, struct scope *s) : prod(p)
{
    if (!s->schema->features.has_savepoint)
        fail("SAVEPOINT not supported by this DBMS");

    // Pick operation type
    auto choice = d6();
    if (choice <= 2)
        type = SAVEPOINT;
    else if (choice <= 4)
        type = RELEASE_SAVEPOINT;
    else
        type = ROLLBACK_TO;

    // Generate a savepoint name
    sp_name = "sp" + to_string(smith::rng() % 100);
}

void savepoint_stmt::out(std::ostream &out)
{
    switch (type) {
        case SAVEPOINT:
            out << "savepoint " << sp_name;
            break;
        case RELEASE_SAVEPOINT:
            out << "release savepoint " << sp_name;
            break;
        case ROLLBACK_TO:
            out << "rollback to savepoint " << sp_name;
            break;
    }
}
