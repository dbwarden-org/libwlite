cmake_minimum_required(VERSION 3.14)
project(wlite VERSION 0.2.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Options
option(WLITE_BUILD_SHARED "Build shared library" ON)
option(WLITE_BUILD_STATIC "Build static library" ON)
option(WLITE_BUILD_CLI "Build CLI executable" ON)
option(WLITE_BUILD_TESTS "Build tests" ON)

# Find SQLite3
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(SQLITE3 QUIET sqlite3)
endif()

if(NOT SQLITE3_FOUND)
    find_package(SQLite3)
endif()

if(NOT SQLITE3_FOUND)
    message(FATAL_ERROR "SQLite3 not found. Install libsqlite3-dev.")
endif()

# Sources
set(WLITE_SOURCES
    wlite/schema.c
    wlite/parser.c
    wlite/introspect.c
    wlite/diff.c
    wlite/planner.c
    wlite/migrate.c
    wlite/serialize.c
    wlite/query.c
    wlite/record.c
    wlite/tx.c
    wlite/schema_inspect.c
    wlite/compile.c
)

set(WLITE_HEADERS
    include/wlite.h
    wlite/internal.h
)

# Static library
if(WLITE_BUILD_STATIC)
    add_library(wlite_static STATIC ${WLITE_SOURCES})
    target_include_directories(wlite_static PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    )
    target_link_libraries(wlite_static PUBLIC SQLite::SQLite3)
    set_target_properties(wlite_static PROPERTIES
        OUTPUT_NAME wlite
        POSITION_INDEPENDENT_CODE ON
    )
endif()

# Shared library
if(WLITE_BUILD_SHARED)
    add_library(wlite_shared SHARED ${WLITE_SOURCES})
    target_include_directories(wlite_shared PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    )
    target_link_libraries(wlite_shared PUBLIC SQLite::SQLite3)
    set_target_properties(wlite_shared PROPERTIES
        OUTPUT_NAME wlite
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
    )
endif()

# CLI
if(WLITE_BUILD_CLI)
    add_executable(wlite-cli cli/main.c)
    target_link_libraries(wlite-cli PRIVATE wlite_static SQLite::SQLite3)
    set_target_properties(wlite-cli PROPERTIES OUTPUT_NAME wlite)
endif()

# Tests
if(WLITE_BUILD_TESTS)
    enable_testing()
    add_executable(test_wlite tests/test_wlite.c)
    target_link_libraries(test_wlite PRIVATE wlite_static SQLite::SQLite3)
    add_test(NAME wlite_tests COMMAND test_wlite)

    add_executable(test_edge tests/test_edge_cases.c)
    target_link_libraries(test_edge PRIVATE wlite_static SQLite::SQLite3)
    add_test(NAME wlite_edge_tests COMMAND test_edge)

    add_executable(test_conformance tests/conformance.c)
    target_link_libraries(test_conformance PRIVATE wlite_static SQLite::SQLite3)
    add_test(NAME wlite_conformance COMMAND test_conformance)
endif()

# Install
include(GNUInstallDirs)

set(WLITE_TARGETS "")
if(WLITE_BUILD_STATIC)
    list(APPEND WLITE_TARGETS wlite_static)
endif()
if(WLITE_BUILD_SHARED)
    list(APPEND WLITE_TARGETS wlite_shared)
endif()
if(WLITE_BUILD_CLI)
    list(APPEND WLITE_TARGETS wlite-cli)
endif()

install(TARGETS
    ${WLITE_TARGETS}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES include/wlite.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/wlite)

# pkg-config
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/wlite.pc.in
    ${CMAKE_CURRENT_BINARY_DIR}/wlite.pc
    @ONLY
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/wlite.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)
