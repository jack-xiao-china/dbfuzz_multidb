# =============================================================
# Bundled Dependencies — Master Orchestrator
# =============================================================
# Included from root CMakeLists.txt when USE_BUNDLED_DEPS=ON.
# Downloads and builds all core DB client libraries from source.
#
# Order matters:
#   1. SQLite3 (no dependencies)
#   2. MariaDB Connector/C (no dependencies)
#   3. libpq (no CMake dependencies, uses autoconf)
#   4. libpqxx (depends on libpq)
#
# After this file is included, the bundled targets/variables are set,
# and the existing find_package blocks will be skipped via guards.

message(STATUS "========================================")
message(STATUS "  Bundled dependency mode ENABLED")
message(STATUS "  All core DB client libraries will be")
message(STATUS "  downloaded and built from source.")
message(STATUS "========================================")

include(FetchContent)

# Set a common prefix for all bundled build artifacts
set(FETCHCONTENT_BASE_DIR ${CMAKE_BINARY_DIR}/bundled/fetchcontent)

# 1. SQLite3 amalgamation (simplest — single .c file)
include(${CMAKE_CURRENT_LIST_DIR}/bundled_sqlite3.cmake)

# 2. MariaDB Connector/C (MySQL-compatible client, CMake-native)
include(${CMAKE_CURRENT_LIST_DIR}/bundled_mariadb.cmake)

# 3. PostgreSQL libpq (ExternalProject + autoconf — most complex)
include(${CMAKE_CURRENT_LIST_DIR}/bundled_libpq.cmake)

# 4. libpqxx (C++ wrapper, depends on libpq — must come after)
include(${CMAKE_CURRENT_LIST_DIR}/bundled_libpqxx.cmake)

message(STATUS "========================================")
message(STATUS "  All bundled dependencies configured")
message(STATUS "========================================")
