# =============================================================
# Bundled libpq (PostgreSQL client library)
# =============================================================
# Downloads PostgreSQL source and builds only libpq using autoconf.
# Provides: PostgreSQL::PostgreSQL IMPORTED target, PostgreSQL_FOUND=TRUE,
#           PostgreSQL_INCLUDE_DIRS, HAVE_POSTGRESQL

include(ExternalProject)

set(LIBPQ_VERSION "REL_17_6")

message(STATUS "Fetching PostgreSQL ${LIBPQ_VERSION} (libpq only)...")

set(LIBPQ_INSTALL_DIR ${CMAKE_BINARY_DIR}/bundled/libpq)

ExternalProject_Add(bundled_libpq
    GIT_REPOSITORY    https://github.com/postgres/postgres.git
    GIT_TAG           ${LIBPQ_VERSION}
    GIT_SHALLOW       TRUE
    GIT_PROGRESS      TRUE

    # Only build src/interfaces/libpq, not the full server
    CONFIGURE_COMMAND
        <SOURCE_DIR>/configure
            --prefix=<INSTALL_DIR>
            --without-icu
            --without-readline
            --without-zlib
            --without-libxml
            --without-libxslt
            --without-ldap
            --without-pam
            --without-bonjour
            --disable-nls
            CC=${CMAKE_C_COMPILER}
            CFLAGS=-fPIC

    BUILD_COMMAND
        make -C src/interfaces/libpq install

    INSTALL_COMMAND ""

    BUILD_IN_SOURCE 1
    BUILD_BYPRODUCTS
        ${LIBPQ_INSTALL_DIR}/lib/libpq.a
)

# Create IMPORTED target matching find_package(PostgreSQL) output
add_library(PostgreSQL::PostgreSQL STATIC IMPORTED GLOBAL)
add_dependencies(PostgreSQL::PostgreSQL bundled_libpq)

set_target_properties(PostgreSQL::PostgreSQL PROPERTIES
    IMPORTED_LOCATION             "${LIBPQ_INSTALL_DIR}/lib/libpq.a"
    INTERFACE_INCLUDE_DIRECTORIES "${LIBPQ_INSTALL_DIR}/include"
    INTERFACE_LINK_LIBRARIES      "ssl;crypto;pthread;dl"
)

set(PostgreSQL_FOUND TRUE)
set(PostgreSQL_INCLUDE_DIRS ${LIBPQ_INSTALL_DIR}/include)

message(STATUS "PostgreSQL libpq ${LIBPQ_VERSION} configured (bundled via ExternalProject)")
message(STATUS "  Install dir: ${LIBPQ_INSTALL_DIR}")
message(STATUS "  Note: requires autoconf, openssl-devel on the build system")
