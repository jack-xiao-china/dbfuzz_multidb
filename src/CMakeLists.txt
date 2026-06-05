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
)
target_include_directories(core_objs PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
)
if(mysqlclient_FOUND)
    target_include_directories(core_objs PRIVATE ${MYSQL_INCLUDE_DIR})
    target_compile_definitions(core_objs PRIVATE HAVE_MYSQL HAVE_TIDB)
    if(HAVE_MYSQL_NONBLOCK)
        target_compile_definitions(core_objs PRIVATE HAVE_MYSQL_NONBLOCK)
    endif()
    if(HAVE_MARIADB)
        target_compile_definitions(core_objs PRIVATE HAVE_MARIADB)
    endif()
endif()
if(SQLite3_FOUND)
    target_compile_definitions(core_objs PRIVATE HAVE_LIBSQLITE3)
endif()

# ============================================================
# grammar_objs
# ============================================================
add_library(grammar_objs OBJECT
    grammar/grammar.cc
)
target_include_directories(grammar_objs PRIVATE ${CMAKE_SOURCE_DIR}/src)

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
    # bool_binop
    expr/bool_expr/bool_binop/distinct_pred.cc
    expr/bool_expr/bool_binop/bool_term.cc
    expr/bool_expr/bool_binop/bool_binop.cc
    expr/bool_expr/bool_binop/comparison_op.cc
)
target_include_directories(expr_objs PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/expr
    ${CMAKE_SOURCE_DIR}/src/expr/bool_expr
    ${CMAKE_SOURCE_DIR}/src/expr/bool_expr/bool_binop
)

# ============================================================
# schema_objs
# ============================================================
set(SCHEMA_SOURCES
    schema/schema.cc
    schema/dut.cc
    schema/postgres.cc
    schema/clickhouse.cc
    schema/yugabyte.cc
    schema/cockroach.cc
)
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
target_include_directories(schema_objs PRIVATE ${CMAKE_SOURCE_DIR}/src)
if(mysqlclient_FOUND)
    target_include_directories(schema_objs PRIVATE ${MYSQL_INCLUDE_DIR})
    target_compile_definitions(schema_objs PRIVATE HAVE_MYSQL HAVE_TIDB)
    if(HAVE_MARIADB)
        target_compile_definitions(schema_objs PRIVATE HAVE_MARIADB)
    endif()
endif()
if(SQLite3_FOUND)
    target_compile_definitions(schema_objs PRIVATE HAVE_LIBSQLITE3)
endif()

# ============================================================
# txcheck_objs
# ============================================================
add_library(txcheck_objs OBJECT
    txcheck/transaction_test.cc
    txcheck/instrumentor.cc
    txcheck/dependency_analyzer.cc
    txcheck/tx_general_process.cc
    # txcheck_run.cc provides txcheck_run() stub; tx_main.cc (original main) excluded
    txcheck/txcheck_run.cc
)
target_include_directories(txcheck_objs PRIVATE ${CMAKE_SOURCE_DIR}/src)

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
    # eet_run.cc provides eet_run() stub; eet_main.cc (original main) excluded
    eet/eet_run.cc
)
target_include_directories(eet_objs PRIVATE ${CMAKE_SOURCE_DIR}/src)

# ============================================================
# cross_objs
# ============================================================
add_library(cross_objs OBJECT
    cross/cross_tester.cc
    cross/cross_main.cc
)
target_include_directories(cross_objs PRIVATE ${CMAKE_SOURCE_DIR}/src)

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
)

target_include_directories(dbfuzz PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
)

target_link_libraries(dbfuzz PRIVATE
    Boost::regex
    PkgConfig::PQXX
    PostgreSQL::PostgreSQL
)

if(mysqlclient_FOUND)
    target_link_libraries(dbfuzz PRIVATE ${MYSQL_LIBRARY})
    target_include_directories(dbfuzz PRIVATE ${MYSQL_INCLUDE_DIR})
endif()

if(SQLite3_FOUND)
    target_link_libraries(dbfuzz PRIVATE SQLite::SQLite3)
endif()
