#include "grammar/lock_stmt.hh"
#include "core/random.hh"

lock_stmt::lock_stmt(prod *p, struct scope *s) : prod(p)
{
    // Choose LOCK or UNLOCK
    is_lock = (d6() > 2);  // 67% LOCK, 33% UNLOCK

    if (is_lock && !s->schema->base_tables.empty()) {
        // Pick a random base table
        auto tab = random_pick(s->schema->base_tables);
        table_name = tab->ident();

        // Pick lock type
        static const char *types[] = {"read", "write", "read local", "write local"};
        lock_type = types[smith::rng() % 4];
    }
}

void lock_stmt::out(std::ostream &out)
{
    if (is_lock)
        out << "lock tables " << table_name << " " << lock_type;
    else
        out << "unlock tables";
}
