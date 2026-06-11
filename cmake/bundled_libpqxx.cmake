# =============================================================
# Bundled libpqxx (C++ PostgreSQL client library)
# =============================================================
# Downloads and builds libpqxx from source as a static library.
# Depends on: bundled_libpq (must be configured first)
# Provides: PkgConfig::PQXX target (alias), PQXX_FOUND=TRUE, PQXX_INCLUDE_DIRS

include(FetchContent)

set(LIBPQXX_VERSION "7.10.0")

message(STATUS "Fetching libpqxx ${LIBPQXX_VERSION}...")

# Disable tests and documentation
set(SKIP_BUILD_TEST     ON  CACHE BOOL "" FORCE)
set(PQXX_SHARED         OFF CACHE BOOL "" FORCE)
set(BUILD_TEST          OFF CACHE BOOL "" FORCE)
set(BUILD_DOC           OFF CACHE BOOL "" FORCE)

# Tell libpqxx where to find PostgreSQL (we provide it ourselves)
set(PostgreSQL_ROOT     ${CMAKE_BINARY_DIR}/bundled/libpq CACHE PATH "" FORCE)

# Prevent libpqxx from calling find_package(PostgreSQL) — we already have the target
set(CMAKE_DISABLE_FIND_PACKAGE_PostgreSQL TRUE CACHE BOOL "" FORCE)

# Provide the variables that libpqxx's CMakeLists.txt expects
set(PostgreSQL_INCLUDE_DIRS "${CMAKE_BINARY_DIR}/bundled/libpq/include" CACHE PATH "" FORCE)
set(PostgreSQL_LIBRARIES    PostgreSQL::PostgreSQL CACHE STRING "" FORCE)

FetchContent_Declare(libpqxx
    GIT_REPOSITORY https://github.com/jtv/libpqxx.git
    GIT_TAG        ${LIBPQXX_VERSION}
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(libpqxx)

# libpqxx creates target 'pqxx'. Create alias matching PkgConfig name.
add_library(PkgConfig::PQXX ALIAS pqxx)
set(PQXX_FOUND TRUE)
set(PQXX_INCLUDE_DIRS ${libpqxx_SOURCE_DIR}/include)

# Propagate include dirs to ALL_INCLUDE_DIRS for object libraries
list(APPEND ALL_INCLUDE_DIRS
    ${libpqxx_SOURCE_DIR}/include
)

message(STATUS "libpqxx ${LIBPQXX_VERSION} configured (bundled)")
