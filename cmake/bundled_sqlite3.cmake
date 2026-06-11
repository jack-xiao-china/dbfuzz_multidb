# =============================================================
# Bundled SQLite3 (amalgamation)
# =============================================================
# Downloads the SQLite3 amalgamation source and builds as a static library.
# Provides: SQLite::SQLite3 target, SQLite3_FOUND=TRUE, HAVE_LIBSQLITE3=1

include(FetchContent)

set(SQLITE3_VERSION "3490200")  # 3.49.2
set(SQLITE3_URL "https://sqlite.org/2025/sqlite-amalgamation-${SQLITE3_VERSION}.zip")

message(STATUS "Fetching SQLite3 amalgamation ${SQLITE3_VERSION}...")

FetchContent_Declare(sqlite3_amalgamation
    URL      ${SQLITE3_URL}
    URL_HASH ""  # Skip hash check for simplicity; can add SHA256 later
)
FetchContent_MakeAvailable(sqlite3_amalgamation)

add_library(bundled_sqlite3 STATIC
    ${sqlite3_amalgamation_SOURCE_DIR}/sqlite3.c
)

target_include_directories(bundled_sqlite3 PUBLIC
    ${sqlite3_amalgamation_SOURCE_DIR}
)

target_compile_definitions(bundled_sqlite3 PRIVATE
    SQLITE_THREADSAFE=1
    SQLITE_DEFAULT_FOREIGN_KEYS=1
    SQLITE_OMIT_LOAD_EXTENSION=1
)

# Suppress warnings in third-party code
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(bundled_sqlite3 PRIVATE -w)
endif()

# Create alias matching find_package(SQLite3) target name
add_library(SQLite::SQLite3 ALIAS bundled_sqlite3)
set(SQLite3_FOUND TRUE)
set(HAVE_LIBSQLITE3 1)

message(STATUS "SQLite3 amalgamation configured (bundled)")
