#pragma once

#include "expr/value_expr.hh"
#include "expr/column_reference.hh"
#include "expr/win_funcall.hh"

/// Window frame clause: ROWS/RANGE/GROUPS BETWEEN ... AND ...
struct window_frame : prod {
    enum frame_mode { ROWS_MODE, RANGE_MODE, GROUPS_MODE };
    enum boundary_type {
        UNBOUNDED_PRECEDING, N_PRECEDING, CURRENT_ROW,
        N_FOLLOWING, UNBOUNDED_FOLLOWING
    };
    frame_mode mode;
    boundary_type start_bound;
    int start_offset;
    boundary_type end_bound;
    int end_offset;

    window_frame(prod *p);
    virtual void out(std::ostream &out);
    virtual ~window_frame() {}
    virtual void accept(prod_visitor *v) { v->visit(this); }
};

struct window_function : value_expr
{
    virtual void out(std::ostream &out);
    virtual ~window_function() {}
    window_function(prod *p, sqltype *type_constraint);
    vector<shared_ptr<column_reference>> partition_by;
    vector<pair<shared_ptr<column_reference>, bool>> order_by;
    shared_ptr<win_funcall> aggregate;
    shared_ptr<window_frame> frame;  // optional frame clause
    static bool allowed(prod *pprod);
    static bool disabled;
    virtual void accept(prod_visitor *v);
    // cannot transfer aggregate, othervwise aggregate will
    // become other value_expr, which cause syntax error
};