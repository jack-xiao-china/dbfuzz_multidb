cmake_minimum_required(VERSION 3.14)
project(dbfuzz VERSION 1.0.8 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Required dependencies ---
find_package(Boost REQUIRED COMPONENTS regex)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PQXX REQUIRED IMPORTED_TARGET libpqxx)
find_package(PostgreSQL REQUIRED)

# --- Optional dependencies ---
find_package(SQLite3)
if(SQLite3_FOUND)
    message(STATUS "SQLite3 found: ${SQLite3_VERSION}")
endif()

# mysqlclient: no standard CMake config, use manual find
find_path(MYSQL_INCLUDE_DIR mysql/mysql.h)
find_library(MYSQL_LIBRARY NAMES mysqlclient)
if(MYSQL_INCLUDE_DIR AND MYSQL_LIBRARY)
    set(mysqlclient_FOUND TRUE)
    message(STATUS "mysqlclient found: ${MYSQL_LIBRARY}")

    # Check for nonblocking API (MySQL 8.x)
    include(CheckSymbolExists)
    set(CMAKE_REQUIRED_INCLUDES ${MYSQL_INCLUDE_DIR})
    set(CMAKE_REQUIRED_LIBRARIES ${MYSQL_LIBRARY})
    check_symbol_exists(mysql_real_query_nonblocking "mysql/mysql.h" HAVE_MYSQL_NONBLOCK)
    check_symbol_exists(mysql_real_query_start "mysql/mysql.h" HAVE_MARIADB)
    unset(CMAKE_REQUIRED_INCLUDES)
    unset(CMAKE_REQUIRED_LIBRARIES)
else()
    set(mysqlclient_FOUND FALSE)
    message(STATUS "mysqlclient not found - MySQL/TiDB/MariaDB/OceanBase support disabled")
endif()

# --- Configure header ---
configure_file(
    ${CMAKE_SOURCE_DIR}/src/config.h.in
    ${CMAKE_BINARY_DIR}/config.h
)

add_subdirectory(src)

# --- Install ---
install(TARGETS dbfuzz RUNTIME DESTINATION bin)
