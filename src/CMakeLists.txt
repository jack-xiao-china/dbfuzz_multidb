# ============================================================
# Common settings applied to all object libraries
# ============================================================
set(ALL_INCLUDE_DIRS
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
)

# schema.hh includes <pqxx/pqxx> unconditionally, so all targets need pqxx includes
if(PQXX_FOUND)
    list(APPEND ALL_INCLUDE_DIRS ${PQXX_INCLUDE_DIRS})
endif()
if(PostgreSQL_FOUND)
    list(APPEND ALL_INCLUDE_DIRS ${PostgreSQL_INCLUDE_DIRS})
endif()

# Build common compile definitions
set(COMMON_DEFS "")
# All feature macros (HAVE_MYSQL, HAVE_TIDB, HAVE_MYSQL_NONBLOCK, HAVE_MARIADB,
# HAVE_LIBSQLITE3, HAVE_BOOST_REGEX) are managed via config.h's #cmakedefine,
# so no -D flags needed here.

# Helper function to apply common settings to a target
function(apply_common target)
    target_include_directories(${target} PRIVATE ${ALL_INCLUDE_DIRS})
    target_compile_definitions(${target} PRIVATE ${COMMON_DEFS})
endfunction()

# ============================================================
# core_objs
# ============================================================
add_library(core_objs OBJECT
    core/relmodel.cc
    core/random.cc
    core/prod.cc
    core/impedance.cc
    core/dump.cc
    core/log.cc
    core/dbms_info.cc
    core/general_process.cc
    core/known_errors.cc
)
apply_common(core_objs)
if(mysqlclient_FOUND)
    target_include_directories(core_objs PRIVATE ${MYSQL_INCLUDE_DIR})
endif()

# ============================================================
# grammar_objs
# ============================================================
add_library(grammar_objs OBJECT
    grammar/grammar.cc
    grammar/savepoint_stmt.cc
    grammar/lock_stmt.cc
)
apply_common(grammar_objs)

# ============================================================
# expr_objs
# ============================================================
add_library(expr_objs OBJECT
    expr/value_expr.cc
    expr/atomic_subselect.cc
    expr/window_function.cc
    expr/coalesce.cc
    expr/funcall.cc
    expr/win_func_using_exist_win.cc
    expr/binop_expr.cc
    expr/const_expr.cc
    expr/case_expr.cc
    expr/win_funcall.cc
    expr/printed_expr.cc
    expr/column_reference.cc
    # bool_expr
    expr/bool_expr/bool_expr.cc
    expr/bool_expr/comp_subquery.cc
    expr/bool_expr/not_expr.cc
    expr/bool_expr/like_op.cc
    expr/bool_expr/between_op.cc
    expr/bool_expr/null_predicate.cc
    expr/bool_expr/exists_predicate.cc
    expr/bool_expr/const_bool.cc
    expr/bool_expr/in_query.cc
    expr/bool_expr/quantified_comparison.cc
    expr/bool_expr/regexp_expr.cc
    expr/bool_expr/member_of_expr.cc
    expr/json_expr.cc
    expr/row_constructor.cc
    expr/cast_expr.cc
    expr/interval_expr.cc
    expr/mysql_json_expr.cc
    # bool_binop
    expr/bool_expr/bool_binop/distinct_pred.cc
    expr/bool_expr/bool_binop/bool_term.cc
    expr/bool_expr/bool_binop/bool_binop.cc
    expr/bool_expr/bool_binop/comparison_op.cc
)
apply_common(expr_objs)

# ============================================================
# schema_objs
# ============================================================
set(SCHEMA_SOURCES
    schema/schema.cc
    schema/dut.cc
)

# PostgreSQL-based schemas (need pqxx)
if(PQXX_FOUND AND PostgreSQL_FOUND)
    list(APPEND SCHEMA_SOURCES
        schema/postgres.cc
        schema/cockroach.cc
        schema/yugabyte.cc
        schema/gaussdb.cc
    )
endif()

# clickhouse doesn't use pqxx directly for schema
list(APPEND SCHEMA_SOURCES schema/clickhouse.cc)

if(mysqlclient_FOUND)
    list(APPEND SCHEMA_SOURCES
        schema/mysql.cc
        schema/tidb.cc
        schema/oceanbase.cc
    )
    if(HAVE_MARIADB)
        list(APPEND SCHEMA_SOURCES schema/mariadb.cc)
    endif()
endif()
if(SQLite3_FOUND)
    list(APPEND SCHEMA_SOURCES schema/sqlite.cc)
endif()

add_library(schema_objs OBJECT ${SCHEMA_SOURCES})
apply_common(schema_objs)
if(mysqlclient_FOUND)
    target_include_directories(schema_objs PRIVATE ${MYSQL_INCLUDE_DIR})
endif()
if(PQXX_FOUND)
    target_include_directories(schema_objs PRIVATE ${PQXX_INCLUDE_DIRS})
endif()
if(PostgreSQL_FOUND)
    target_include_directories(schema_objs PRIVATE ${PostgreSQL_INCLUDE_DIRS})
endif()

# ============================================================
# txcheck_objs
# ============================================================
add_library(txcheck_objs OBJECT
    txcheck/transaction_test.cc
    txcheck/instrumentor.cc
    txcheck/dependency_analyzer.cc
    txcheck/tx_general_process.cc
    txcheck/txcheck_run.cc
)
apply_common(txcheck_objs)
if(mysqlclient_FOUND)
    target_include_directories(txcheck_objs PRIVATE ${MYSQL_INCLUDE_DIR})
endif()

# ============================================================
# eet_objs
# ============================================================
add_library(eet_objs OBJECT
    eet/qcn_tester/qcn_tester.cc
    eet/qcn_tester/qcn_select_tester.cc
    eet/qcn_tester/qcn_update_tester.cc
    eet/qcn_tester/qcn_delete_tester.cc
    eet/qcn_tester/qcn_cte_tester.cc
    eet/qcn_tester/qcn_insert_select_tester.cc
    eet/eet_general_process.cc
    eet/eet_run.cc
)
apply_common(eet_objs)
if(mysqlclient_FOUND)
    target_include_directories(eet_objs PRIVATE ${MYSQL_INCLUDE_DIR})
endif()

# ============================================================
# cross_objs
# ============================================================
add_library(cross_objs OBJECT
    cross/cross_tester.cc
    cross/cross_main.cc
)
apply_common(cross_objs)

# ============================================================
# smoke_objs
# ============================================================
add_library(smoke_objs OBJECT
    smoke/smoke_run.cc
)
apply_common(smoke_objs)

# ============================================================
# Final executable
# ============================================================
add_executable(dbfuzz
    main.cc
    $<TARGET_OBJECTS:core_objs>
    $<TARGET_OBJECTS:grammar_objs>
    $<TARGET_OBJECTS:expr_objs>
    $<TARGET_OBJECTS:schema_objs>
    $<TARGET_OBJECTS:txcheck_objs>
    $<TARGET_OBJECTS:eet_objs>
    $<TARGET_OBJECTS:cross_objs>
    $<TARGET_OBJECTS:smoke_objs>
)

target_include_directories(dbfuzz PRIVATE ${ALL_INCLUDE_DIRS})
target_compile_definitions(dbfuzz PRIVATE ${COMMON_DEFS})

# --- Link libraries ---

# Boost regex (optional)
if(HAVE_BOOST_REGEX)
    target_link_libraries(dbfuzz PRIVATE Boost::regex)
endif()

# libpqxx + PostgreSQL (optional)
if(PQXX_FOUND AND PostgreSQL_FOUND)
    target_link_libraries(dbfuzz PRIVATE PkgConfig::PQXX PostgreSQL::PostgreSQL)
endif()

# MySQL client (optional)
if(mysqlclient_FOUND)
    target_link_libraries(dbfuzz PRIVATE ${MYSQL_LIBRARY})
    target_include_directories(dbfuzz PRIVATE ${MYSQL_INCLUDE_DIR})
endif()

# SQLite3 (optional)
if(SQLite3_FOUND)
    target_link_libraries(dbfuzz PRIVATE SQLite::SQLite3)
endif()

# Windows-specific libraries
if(WIN32)
    target_link_libraries(dbfuzz PRIVATE ws2_32)
endif()
