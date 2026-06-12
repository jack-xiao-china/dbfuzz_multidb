/// @file
/// @brief grammar: Top-level and unsorted grammar productions

#ifndef GRAMMAR_HH
#define GRAMMAR_HH

#include <ostream>
#include "core/relmodel.hh"
#include <memory>
#include "schema/schema.hh"

#include "core/prod.hh"
#include "expr/expr.hh"

#include <set>
using std::shared_ptr;

struct table_ref : prod {
  vector<shared_ptr<named_relation> > refs;
  static shared_ptr<table_ref> factory(prod *p, bool no_join = false);
  table_ref(prod *p) : prod(p) { }
  virtual ~table_ref() { }
};

struct table_or_query_name : table_ref {
    virtual void out(std::ostream &out);
    table_or_query_name(prod *p, bool only_base_table = false);
    table_or_query_name(prod *p, table *target_table);
    virtual ~table_or_query_name() { }
    named_relation *t;
    string index_hint;  // MySQL: USE/FORCE/IGNORE INDEX (idx_list)
    string partition_hint;  // MySQL: PARTITION (p0, p1)
};

struct target_table : table_ref {
  virtual void out(std::ostream &out);
  target_table(prod *p, table *victim = 0);
  virtual ~target_table() { }
  table *victim_;
};

struct table_sample : table_ref {
  virtual void out(std::ostream &out);
  table_sample(prod *p);
  virtual ~table_sample() { }
  struct table *t;
private:
  string method;
  double percent;
};

struct table_subquery : table_ref {
  bool is_lateral;
  virtual void out(std::ostream &out);
  shared_ptr<struct query_spec> query;
  table_subquery(prod *p, bool lateral = false);
  virtual ~table_subquery();
  virtual void accept(prod_visitor *v);
};

struct lateral_subquery : table_subquery {
  lateral_subquery(prod *p)
    : table_subquery(p, true) {  }
};

struct join_cond : prod {
     static shared_ptr<join_cond> factory(prod *p, table_ref &lhs, table_ref &rhs);
     join_cond(prod *p, table_ref &lhs, table_ref &rhs)
	  : prod(p) { (void) lhs; (void) rhs;}
};

struct simple_join_cond : join_cond {
     std::string condition;
     simple_join_cond(prod *p, table_ref &lhs, table_ref &rhs);
     virtual void out(std::ostream &out);
};

struct expr_join_cond : join_cond {
     struct scope joinscope;
     shared_ptr<bool_expr> search;
     expr_join_cond(prod *p, table_ref &lhs, table_ref &rhs);
     virtual void out(std::ostream &out);
     virtual void accept(prod_visitor *v) {
	  search->accept(v);
	  v->visit(this);
     }
};

struct joined_table : table_ref {
    virtual void out(std::ostream &out);  
    joined_table(prod *p);
    std::string type;
    std::string alias;
    virtual std::string ident() { return alias; }
    shared_ptr<table_ref> lhs;
    shared_ptr<table_ref> rhs;
    shared_ptr<join_cond> condition;
    virtual ~joined_table() {
    }
    virtual void accept(prod_visitor *v) {
        lhs->accept(v);
        rhs->accept(v);
        if (type == "inner" || type == "left outer")
            condition->accept(v);
        v->visit(this);
    }
};

struct from_clause : prod {
    std::vector<shared_ptr<table_ref> > reflist;
    virtual void out(std::ostream &out);
    from_clause(prod *p, bool only_base_table = false);
    from_clause(prod *p, table *from_table);
    ~from_clause() { }
    virtual void accept(prod_visitor *v) {
        v->visit(this);
        for (auto p : reflist)
            p->accept(v);
    }
};

struct select_list : prod {
    vector<shared_ptr<value_expr> > value_exprs;
    vector<shared_ptr<named_relation> > *prefer_refs;
    relation derived_table;
    int columns = 0;
    select_list(prod *p, 
              vector<shared_ptr<named_relation> > *refs = 0, 
              vector<sqltype *> *pointed_type = NULL,
              bool select_all = false);
    virtual void out(std::ostream &out);
    ~select_list() { }
    virtual void accept(prod_visitor *v) {
        v->visit(this);
        for (auto p : value_exprs)
            p->accept(v);
    }
};

struct group_clause: prod {
    enum group_type { GROUP_SIMPLE, GROUP_GROUPING_SETS, GROUP_CUBE, GROUP_ROLLUP };
    group_type type;
    bool with_rollup = false;  // MySQL: GROUP BY ... WITH ROLLUP
    struct scope myscope;
    shared_ptr<struct select_list> modified_select_list;
    vector<shared_ptr<named_relation>> tmp_store; // let new_relation not to be freed so far (for having clause)
    shared_ptr<bool_expr> having_cond_search;
    group_clause(prod *p, struct scope *s,
            shared_ptr<struct select_list> select_list,
            std::vector<shared_ptr<named_relation> > *from_refs);
    shared_ptr<column_reference> target_ref;
    // GROUPING SETS / CUBE / ROLLUP members
    vector<shared_ptr<column_reference>> cube_rollup_cols;
    vector<vector<shared_ptr<column_reference>>> group_sets;
    virtual void out(std::ostream &out);
    virtual void accept(prod_visitor *v) {
        v->visit(this);
    }
};

struct named_window : prod {
    struct scope myscope;
    virtual void out(std::ostream &out);
    virtual ~named_window() { }
    named_window(prod *p, struct scope *s);
    string window_name;
    vector<shared_ptr<value_expr> > partition_by;
    vector< pair<shared_ptr<value_expr>, bool> > order_by;
    shared_ptr<window_frame> frame;  // optional frame clause
    bool asc;

    virtual void accept(prod_visitor *v) {
        v->visit(this);
        for (auto p : partition_by)
            p->accept(v);
        for (auto p : order_by)
            p.first->accept(v);
        if (frame)
            frame->accept(v);
    }
};

struct query_spec : prod {
    string set_quantifier;
    string mysql_select_options;  // MySQL: SQL_CALC_FOUND_ROWS, etc.
    shared_ptr<struct from_clause> from_clause;
    shared_ptr<struct select_list> select_list;
    shared_ptr<bool_expr> search;
    
    bool has_group = false;
    shared_ptr<struct group_clause> group_clause;

    bool has_window = false;
    shared_ptr<struct named_window> window_clause;

    bool has_order = false;
    vector<pair<string, bool> > order_clause;
    
    bool has_limit = false;
    int limit_num;

    struct scope myscope;
    virtual void out(std::ostream &out);

    query_spec(prod *p, struct scope *s,
                bool lateral = 0, 
                vector<sqltype *> *pointed_type = NULL,
                bool txn_mode = false);
    
    query_spec(prod *p, struct scope *s,
              table *from_table, 
              shared_ptr<bool_expr> where_search);
    
    query_spec(prod *p, struct scope *s,
              table *from_table, 
              op *target_op, 
              shared_ptr<value_expr> left_operand,
              shared_ptr<value_expr> right_operand);
    
    virtual void accept(prod_visitor *v) {
        v->visit(this);
        select_list->accept(v);
        from_clause->accept(v);
        search->accept(v);
        if (has_group)
            group_clause->accept(v);
        if (has_window)
            window_clause->accept(v);
    }
};

struct select_for_update : query_spec {
  const char *lockmode;
  virtual void out(std::ostream &out);
  select_for_update(prod *p, struct scope *s, bool lateral = 0);
};

struct prepare_stmt : prod {
  query_spec q;
  static long seq;
  long id;
  virtual void out(std::ostream &out) {
    out << "prepare prep" << id << " as " << q;
  }
  prepare_stmt(prod *p) : prod(p), q(p, scope) {
    id = seq++;
  }
  virtual void accept(prod_visitor *v) {
    v->visit(this);
    q.accept(v);
  }
};

/// EXECUTE a previously PREPAREd statement
struct execute_stmt : prod {
    long prep_id;
    static long last_prep_id;
    execute_stmt(prod *p);
    virtual void out(std::ostream &out) {
        out << "EXECUTE prep" << prep_id;
    }
    virtual void accept(prod_visitor *v) { v->visit(this); }
};

struct modifying_stmt : prod {
    table *victim;
    struct scope myscope;
    modifying_stmt(prod *p, struct scope *s, struct table *v = 0);
//   shared_ptr<modifying_stmt> modifying_stmt::factory(prod *p, struct scope *s);
    virtual void pick_victim();
};

struct delete_stmt : modifying_stmt {
    shared_ptr<bool_expr> search;
    bool has_order_limit = false;
    string order_col;
    bool order_asc = true;
    int limit_num = 0;
    delete_stmt(prod *p, struct scope *s, table *v = 0);
    virtual ~delete_stmt() { }
    virtual void out(ostream &out);
    virtual void accept(prod_visitor *v) {
        v->visit(this);
        search->accept(v);
    }
};

struct delete_returning : delete_stmt {
  shared_ptr<struct select_list> select_list;
  delete_returning(prod *p, struct scope *s, table *victim = 0);
  virtual void out(std::ostream &out) {
    delete_stmt::out(out);
    out << std::endl << "returning " << *select_list;
  }
  virtual void accept(prod_visitor *v) {
    v->visit(this);
    search->accept(v);
    select_list->accept(v);
  }
};

struct insert_stmt : modifying_stmt {
    vector<vector<shared_ptr<value_expr> > > value_exprs_vector;
    // vector<shared_ptr<value_expr> > value_exprs;
    vector<string> valued_column_name;
    insert_stmt(prod *p, struct scope *s, table *victim = 0, bool only_const = false);
    virtual ~insert_stmt() {  }
    virtual void out(std::ostream &out);
    virtual void accept(prod_visitor *v) {
        v->visit(this);
        for (auto &value_exprs: value_exprs_vector)
            for (auto p : value_exprs)
                p->accept(v);
    }
};

/// MySQL REPLACE statement (sql_yacc.yy replace_stmt)
/// REPLACE INTO table (cols) VALUES (...) — like INSERT but deletes old row on duplicate key
struct replace_stmt : insert_stmt {
    replace_stmt(prod *p, struct scope *s, table *victim = 0);
    virtual ~replace_stmt() { }
    virtual void out(std::ostream &out);
};

/// MySQL DO statement (sql_yacc.yy do_stmt)
/// DO expr1, expr2, ... — evaluates expressions without returning results
struct do_stmt : prod {
    vector<shared_ptr<value_expr>> exprs;
    do_stmt(prod *p, struct scope *s);
    virtual ~do_stmt() { }
    virtual void out(std::ostream &out);
    virtual void accept(prod_visitor *v) {
        v->visit(this);
        for (auto &e : exprs) e->accept(v);
    }
};

/// MySQL EXPLAIN / EXPLAIN ANALYZE statement (sql_yacc.yy explain_stmt)
/// EXPLAIN [FORMAT = JSON|TREE|TRADITIONAL] select_stmt
/// EXPLAIN ANALYZE select_stmt
struct explain_stmt : prod {
    string explain_type;  // "EXPLAIN", "EXPLAIN ANALYZE", "EXPLAIN FORMAT=JSON", "EXPLAIN FORMAT=TREE"
    shared_ptr<prod> inner_stmt;
    explain_stmt(prod *p, struct scope *s);
    virtual ~explain_stmt() { }
    virtual void out(std::ostream &out);
    virtual void accept(prod_visitor *v) {
        v->visit(this);
        inner_stmt->accept(v);
    }
};

/// MySQL table maintenance statements (CHECKSUM/CHECK/OPTIMIZE/REPAIR TABLE)
struct table_maintenance_stmt : prod {
    string command;  // "CHECKSUM TABLE", "CHECK TABLE", "OPTIMIZE TABLE", "REPAIR TABLE"
    table *victim;
    table_maintenance_stmt(prod *p, struct scope *s);
    virtual ~table_maintenance_stmt() { }
    virtual void out(std::ostream &out);
    virtual void accept(prod_visitor *v) { v->visit(this); }
};

struct set_list : prod {
    struct scope myscope;
    vector<shared_ptr<value_expr> > value_exprs;
    vector<string> names;
    std::set<string> name_set;
    set_list(prod *p, table *target);
    virtual ~set_list() {  }
    virtual void out(std::ostream &out);
    virtual void accept(prod_visitor *v) {
        v->visit(this);
        for (auto p : value_exprs) 
            p->accept(v);
    }
};

struct upsert_stmt : insert_stmt {
  shared_ptr<struct set_list> set_list;
  string constraint;
  shared_ptr<bool_expr> search;
  upsert_stmt(prod *p, struct scope *s, table *v = 0);
  virtual void out(std::ostream &out) {
    insert_stmt::out(out);
    if (scope->schema->features.has_on_duplicate_key) {
      // MySQL/TiDB/MariaDB/OceanBase syntax
      out << " on duplicate key update ";
      out << *set_list;
    } else {
      // PostgreSQL syntax
      out << " on conflict on constraint " << constraint << " do update ";
      out << *set_list << " where " << *search;
    }
  }
  virtual void accept(prod_visitor *v) {
    insert_stmt::accept(v);
    set_list->accept(v);
    search->accept(v);
  }
  virtual ~upsert_stmt() {  }
};

struct update_stmt : modifying_stmt {
    shared_ptr<bool_expr> search;
    shared_ptr<struct set_list> set_list;
    bool has_order_limit = false;
    string order_col;
    bool order_asc = true;
    int limit_num = 0;
    update_stmt(prod *p, struct scope *s, table *victim = 0);
    virtual ~update_stmt() {  }
    virtual void out(std::ostream &out);
    virtual void accept(prod_visitor *v) {
        v->visit(this);
        search->accept(v);
    }
};

struct when_clause : prod {
  bool matched;
  shared_ptr<bool_expr> condition;  
//   shared_ptr<prod> merge_action;
  when_clause(struct merge_stmt *p);
  virtual ~when_clause() { }
  static shared_ptr<when_clause> factory(struct merge_stmt *p);
  virtual void out(std::ostream &out);
  virtual void accept(prod_visitor *v);
};

struct when_clause_update : when_clause {
  shared_ptr<struct set_list> set_list;
  struct scope myscope;
  when_clause_update(struct merge_stmt *p);
  virtual ~when_clause_update() { }
  virtual void out(std::ostream &out);
  virtual void accept(prod_visitor *v);
};

struct when_clause_insert : when_clause {
  vector<shared_ptr<value_expr> > exprs;
  when_clause_insert(struct merge_stmt *p);
  virtual ~when_clause_insert() { }
  virtual void out(std::ostream &out);
  virtual void accept(prod_visitor *v);
};

struct merge_stmt : modifying_stmt {
  merge_stmt(prod *p, struct scope *s, table *victim = 0);
  shared_ptr<table_ref> target_table_;
  shared_ptr<table_ref> data_source;
  shared_ptr<join_cond> join_condition;
  vector<shared_ptr<when_clause> > clauselist;
  virtual ~merge_stmt() {  }
  virtual void out(std::ostream &out);
  virtual void accept(prod_visitor *v);
};

struct update_returning : update_stmt {
  shared_ptr<struct select_list> select_list;
  update_returning(prod *p, struct scope *s, table *victim = 0);
  virtual void out(std::ostream &out) {
    update_stmt::out(out);
    out << std::endl << "returning " << *select_list;
  }
  virtual void accept(prod_visitor *v) {
    v->visit(this);
    search->accept(v);
    set_list->accept(v);
    select_list->accept(v);
  }
};

struct common_table_expression : prod {
    vector<shared_ptr<query_spec> > with_queries;
    shared_ptr<query_spec> query;
    vector<shared_ptr<named_relation> > refs;
    struct scope myscope;
    bool is_recursive = false;  // WITH RECURSIVE
    virtual void out(std::ostream &out);
    virtual void accept(prod_visitor *v);
    common_table_expression(prod *parent, struct scope *s, bool txn_mode = false);
};

/// CTE data-modifying item: INSERT/UPDATE/DELETE + RETURNING inside WITH
struct cte_dml_item : prod {
    enum dml_type { CTE_INSERT, CTE_UPDATE, CTE_DELETE };
    dml_type type;
    shared_ptr<prod> dml_stmt;
    table *victim;
    struct scope myscope;
    cte_dml_item(prod *p, struct scope *s, table *v);
    virtual void out(std::ostream &out);
    virtual void accept(prod_visitor *v);
};

/// Data-modifying CTE: WITH ... AS (INSERT/UPDATE/DELETE RETURNING *) SELECT ...
struct data_modifying_cte : prod {
    vector<shared_ptr<cte_dml_item>> cte_items;
    vector<shared_ptr<named_relation>> cte_refs;
    shared_ptr<query_spec> final_query;
    struct scope myscope;
    data_modifying_cte(prod *p, struct scope *s, bool txn_mode = false);
    virtual void out(std::ostream &out);
    virtual void accept(prod_visitor *v);
};

/// Partition table metadata for tracking across statements
struct partition_info {
    string partition_type;           // "RANGE", "LIST", "HASH", "KEY"
    vector<string> partition_names;  // ["p0", "p1", ...]
    string partition_col;            // partition key column name
    bool has_subpartition = false;
};
extern std::map<string, partition_info> table_partitions;

struct create_table_stmt: prod {
    shared_ptr<struct table> created_table;
    struct scope myscope;

    vector<string> constraints;
    bool has_option = false;
    string table_option;
    bool has_check = false;
    shared_ptr<struct bool_expr> check_expr;
    bool has_engine = false;
    string table_engine;
    bool has_primary_key = false;
    bool has_foreign_key = false;
    int primary_col_id;
    int partition_col_id = -1;  // column index used for partitioning (-1 = none)
    int subpartition_col_id = -1;  // column index for sub-partitioning (-1 = none)
    vector<string> partition_subtable_stmts;  // PG: CREATE TABLE ... PARTITION OF statements

    // Generated columns (MySQL 8.0 P0)
    struct gen_col_def {
        string name;
        string type_name;
        string expr_str;
        string storage;  // "virtual" or "stored"
    };
    vector<gen_col_def> generated_columns;
    virtual void out(std::ostream &out);
    create_table_stmt(prod *parent, struct scope *s);
    virtual void accept(prod_visitor *v) {
        v->visit(this);
        if (has_check)
            check_expr->accept(v);
    }
};

struct create_view_stmt: prod {
    string tatble_name;
    shared_ptr<struct query_spec> subquery;
    struct scope myscope;
    virtual void out(std::ostream &out);
    create_view_stmt(prod *parent, struct scope *s);
    virtual void accept(prod_visitor *v) {
        subquery->accept(v);
        v->visit(this);
    }
};

struct alter_table_stmt: prod {
    // shared_ptr<struct table> created_table;
    struct scope myscope;
    int stmt_type; // 0: rename table, 1: rename column, 2: add column, 3: drop column
                   // 4: modify column, 5: add index
                   // 6: ADD PARTITION, 7: DROP PARTITION, 8: TRUNCATE PARTITION
                   // 9: COALESCE PARTITION, 10: REORGANIZE PARTITION
                   // 11: ANALYZE/CHECK/OPTIMIZE/REBUILD/REPAIR PARTITION
                   // 12: REMOVE PARTITIONING
                   // 13: ATTACH PARTITION (PG), 14: DETACH PARTITION (PG)
    string stmt_string;
    virtual void out(std::ostream &out);
    alter_table_stmt(prod *parent, struct scope *s);
    virtual void accept(prod_visitor *v) {
        v->visit(this);
    }
};

struct drop_table_stmt: prod {
    // shared_ptr<struct table> created_table;
    struct scope myscope;
    string stmt_string;
    virtual void out(std::ostream &out);
    drop_table_stmt(prod *parent, struct scope *s);
    virtual void accept(prod_visitor *v) {
        v->visit(this);
    }
};

struct create_index_stmt: prod {
    struct scope myscope;
    string index_name;
    // cannot refer to table* or column*, because the (table or column) instances may die when output
    string indexed_table_name;
    vector<string> indexed_column_names; 
    vector<string> asc_desc_empty;
    vector<bool> has_collation;
    vector<string> collation;
    shared_ptr<bool_expr> where_expr;
    bool is_unique;
    create_index_stmt(prod *parent, struct scope *s);
    virtual void out(std::ostream &out);
    virtual void accept(prod_visitor *v) {
        v->visit(this);
    }
};

struct create_trigger_stmt: prod {
    struct scope myscope;
    string trigger_name;
    string trigger_time; // after or before
    string trigger_event; // delete, insert or update
    string table_name;
    vector<shared_ptr<struct modifying_stmt>> doing_stmts;

    virtual void out(std::ostream &out);
    create_trigger_stmt(prod *parent, struct scope *s);
    virtual void accept(prod_visitor *v) {
        v->visit(this);
        for (auto &stmt : doing_stmts)
            stmt->accept(v); 
    }
};

struct unioned_query : prod { //can be used as same as query_spec
    struct scope myscope;
    shared_ptr<query_spec> lhs;
    shared_ptr<query_spec> rhs;
    string type; // union, union all, intersect, or except
    virtual void out(std::ostream &out);  
    unioned_query(prod *p, struct scope *s, bool lateral = 0, vector<sqltype *> *pointed_type = NULL);
    unioned_query(prod *p, struct scope *s, shared_ptr<query_spec> q_lhs, shared_ptr<query_spec> q_rhs, string u_type);
    void equivalent_transform();
    void back_transform();
    shared_ptr<prod> eq_query;
    bool is_transformed = false;
    virtual ~unioned_query() {}
    virtual void accept(prod_visitor *v) {
        lhs->accept(v);
        rhs->accept(v);
        v->visit(this);
    }
};

struct insert_select_stmt : modifying_stmt {
    shared_ptr<struct query_spec> target_subquery;
    vector<string> valued_column_name;
    insert_select_stmt(prod *p, struct scope *s, table *victim = 0);
    virtual ~insert_select_stmt() {  }
    virtual void out(std::ostream &out);
    virtual void accept(prod_visitor *v) {
        v->visit(this);
        target_subquery->accept(v);
    }
};

struct txn_string_stmt : prod {
  string stmt;
  txn_string_stmt(prod *p, string bs) : 
    prod(p), stmt(bs) {}
  virtual ~txn_string_stmt() {}
  virtual void out(std::ostream &out) {out << stmt;}
  virtual void accept(prod_visitor *v) {
    v->visit(this);
  }
};

struct analyze_stmt : prod {
  struct scope myscope;
  string target; // can be "" (empty), table_name, index_name
  analyze_stmt(prod *p, struct scope *s);
  virtual ~analyze_stmt() {}
  virtual void out(std::ostream &out);
  virtual void accept(prod_visitor *v) {
    v->visit(this);
  }
};

struct set_stmt : prod {
  struct scope myscope;
  string parm;
  string value;
  set_stmt(prod *p, struct scope *s);
  virtual ~set_stmt() {}
  virtual void out(std::ostream &out);
  virtual void accept(prod_visitor *v) {
    v->visit(this);
  }
};

// SET TRANSACTION ISOLATION LEVEL (MySQL 8.0 P0)
struct set_isolation_stmt : prod {
  string isolation_level;
  set_isolation_stmt(prod *p, struct scope *s);
  virtual ~set_isolation_stmt() {}
  virtual void out(std::ostream &out);
  virtual void accept(prod_visitor *v) { v->visit(this); }
};

// Multi-table UPDATE (MySQL 8.0 P0)
// UPDATE t1, t2 SET t1.a = expr WHERE t1.id = t2.id
struct multi_table_update_stmt : prod {
    struct scope myscope;
    shared_ptr<table_ref> table_refs;
    vector<pair<named_relation*, shared_ptr<struct set_list>>> set_clauses;
    shared_ptr<bool_expr> search;
    multi_table_update_stmt(prod *p, struct scope *s);
    virtual ~multi_table_update_stmt() {}
    virtual void out(std::ostream &out);
    virtual void accept(prod_visitor *v) { v->visit(this); search->accept(v); }
};

// Multi-table DELETE (MySQL 8.0 P0)
// DELETE t1 [, t2] FROM table_references WHERE condition
struct multi_table_delete_stmt : prod {
    struct scope myscope;
    vector<string> delete_targets;
    shared_ptr<table_ref> table_refs;
    shared_ptr<bool_expr> search;
    multi_table_delete_stmt(prod *p, struct scope *s);
    virtual ~multi_table_delete_stmt() {}
    virtual void out(std::ostream &out);
    virtual void accept(prod_visitor *v) { v->visit(this); search->accept(v); }
};

// XA transaction statements (MySQL 8.0 P0)
struct xa_stmt : prod {
  enum xa_type { XA_START, XA_END, XA_PREPARE, XA_COMMIT, XA_ROLLBACK };
  xa_type type;
  string xid;
  bool one_phase;
  static int xa_counter;
  xa_stmt(prod *p, struct scope *s);
  virtual ~xa_stmt() {}
  virtual void out(std::ostream &out);
  virtual void accept(prod_visitor *v) { v->visit(this); }
};

#define SPACE_HOLDER_STMT "select 1 from (select 1) as subq_0 where 0 <> 0"

extern int write_op_id;

shared_ptr<prod> statement_factory(struct scope *s);
shared_ptr<prod> ddl_statement_factory(struct scope *s);
shared_ptr<prod> basic_dml_statement_factory(struct scope *s);
shared_ptr<prod> txn_statement_factory(struct scope *s, int choice = -1);
string unique_column_name();
void clear_naming_data ();

#endif
