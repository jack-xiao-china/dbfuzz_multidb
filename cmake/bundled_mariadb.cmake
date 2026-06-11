# =============================================================
# Bundled MariaDB Connector/C (MySQL-compatible client library)
# =============================================================
# Downloads and builds MariaDB Connector/C from source as a static library.
# Provides: mysqlclient_FOUND=TRUE, MYSQL_INCLUDE_DIR, MYSQL_LIBRARY,
#           HAVE_MYSQL=1, HAVE_TIDB=1

include(FetchContent)

set(MARIADB_CONNECTOR_VERSION "v3.4.5")

message(STATUS "Fetching MariaDB Connector/C ${MARIADB_CONNECTOR_VERSION}...")

# Disable features we don't need BEFORE FetchContent_MakeAvailable
set(WITH_SSL            "OPENSSL" CACHE STRING "" FORCE)
set(WITH_UNIT_TESTS     OFF      CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS   OFF      CACHE BOOL "" FORCE)
set(WITH_EXTERNAL_ZLIB  OFF      CACHE BOOL "" FORCE)
set(INSTALL_PLUGINDIR   "unused" CACHE STRING "" FORCE)
set(WITH_CURL           OFF      CACHE BOOL "" FORCE)

# Disable test and example builds
set(MARIADB_INTERACTIVE_BUILD OFF CACHE BOOL "" FORCE)

FetchContent_Declare(mariadb_connector
    GIT_REPOSITORY https://github.com/mariadb-corporation/mariadb-connector-c.git
    GIT_TAG        ${MARIADB_CONNECTOR_VERSION}
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(mariadb_connector)

# MariaDB Connector/C creates target 'mariadbclient'
# Set variables matching what src/CMakeLists.txt expects
set(MYSQL_INCLUDE_DIR
    ${mariadb_connector_SOURCE_DIR}/include
    ${mariadb_connector_BINARY_DIR}/include
    CACHE PATH "MySQL include directory (bundled MariaDB Connector/C)" FORCE
)

# The library target name varies; mariadbclient is the standard name
set(MYSQL_LIBRARY mariadbclient CACHE STRING "MySQL client library (bundled)" FORCE)
set(mysqlclient_FOUND TRUE CACHE BOOL "" FORCE)
set(HAVE_MYSQL 1)
set(HAVE_TIDB 1)

# MariaDB Connector/C always provides MariaDB async API
set(HAVE_MARIADB 1)

# Note: MariaDB Connector/C does NOT provide mysql_real_query_nonblocking
# (that's MySQL 8.x specific). The code handles this via config.h #ifdef.

# Propagate include dirs to ALL_INCLUDE_DIRS for object libraries
list(APPEND ALL_INCLUDE_DIRS
    ${mariadb_connector_SOURCE_DIR}/include
    ${mariadb_connector_BINARY_DIR}/include
)

message(STATUS "MariaDB Connector/C ${MARIADB_CONNECTOR_VERSION} configured (bundled)")
message(STATUS "  HAVE_MYSQL=1, HAVE_TIDB=1, HAVE_MARIADB=1")
message(STATUS "  HAVE_MYSQL_NONBLOCK=0 (not available in MariaDB Connector/C)")
