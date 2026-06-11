cmake_minimum_required(VERSION 3.14)
project(dbfuzz VERSION 1.0.18 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Required dependencies ---

# Boost regex: optional, code falls back to std::regex when unavailable
find_package(Boost COMPONENTS regex)
if(Boost_FOUND AND Boost_REGEX_FOUND)
    set(HAVE_BOOST_REGEX TRUE)
    message(STATUS "Boost.Regex found")
else()
    set(HAVE_BOOST_REGEX FALSE)
    message(STATUS "Boost.Regex not found - using std::regex fallback")
endif()

# PkgConfig for libpqxx
find_package(PkgConfig)

# libpqxx: optional (needed for PostgreSQL schema)
if(PkgConfig_FOUND)
    pkg_check_modules(PQXX IMPORTED_TARGET libpqxx)
endif()

# PostgreSQL: optional
find_package(PostgreSQL)

if(NOT PQXX_FOUND OR NOT PostgreSQL_FOUND)
    message(STATUS "libpqxx or PostgreSQL not found - PostgreSQL schema support disabled")
endif()

# --- Optional dependencies ---
find_package(SQLite3)
if(SQLite3_FOUND)
    set(HAVE_LIBSQLITE3 1)
    message(STATUS "SQLite3 found: ${SQLite3_VERSION}")
endif()

# mysqlclient: no standard CMake config, use manual find
find_path(MYSQL_INCLUDE_DIR mysql/mysql.h)
find_library(MYSQL_LIBRARY NAMES mysqlclient)
if(MYSQL_INCLUDE_DIR AND MYSQL_LIBRARY)
    set(mysqlclient_FOUND TRUE)
    set(HAVE_MYSQL 1)
    set(HAVE_TIDB 1)
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

# --- Platform detection ---
if(WIN32)
    message(STATUS "Building for Windows (MinGW/MSVC)")
endif()

add_subdirectory(src)

# --- Install ---
install(TARGETS dbfuzz RUNTIME DESTINATION bin)
