# test/CMakeLists.txt — Google Test integration for Taurus.
#
# Each subdirectory under test/ maps to one module under src/taurus/.
# Adding a new test executable = one call to taurus_add_test().

# Fetch Google Test if the consumer hasn't provided one.  We prefer a
# system/vcpkg install when available; FetchContent is the fallback so
# local builds work without a package manager.
if(NOT TARGET GTest::gtest_main)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.14.0
    )
    set(gtest_force_shared_rt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

include(GoogleTest)

# Convenience aggregate target — `cmake --build build --target check_all`.
add_custom_target(check_all
    COMMENT "Running all Taurus specs")

# taurus_add_test(<name> <sources...>)
#
# Creates an executable, links it to libtaurus + gtest_main, registers
# it with ctest via gtest_discover_tests, and adds it to check_all.
function(taurus_add_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE taurus GTest::gtest_main)
    target_include_directories(${name} PRIVATE
        ${PROJECT_SOURCE_DIR}/src/include
        # Pool / StringView internals — used by memory specs.
        ${PROJECT_SOURCE_DIR}/src/taurus/memory
        ${PROJECT_SOURCE_DIR}/src/taurus/common
        # DOM internals — used by vtable specs.
        ${PROJECT_SOURCE_DIR}/src/taurus/dom
        # Flat doc internals — used by flat parse specs (TODO 139).
        ${PROJECT_SOURCE_DIR}/src/taurus/flat)
    target_compile_features(${name} PRIVATE cxx_std_11)
    gtest_discover_tests(${name} DISCOVERY_MODE PRE_TEST)
    add_dependencies(check_all ${name})
endfunction()

# Specialization for CLI specs, which invoke the built binary via
# subprocess and need to know its path.
function(taurus_add_cli_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE GTest::gtest_main)
    target_compile_features(${name} PRIVATE cxx_std_11)
    target_compile_definitions(${name} PRIVATE
        TAURUS_CLI_BIN="${CMAKE_BINARY_DIR}/cli/taurus"
        TAURUS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    gtest_discover_tests(${name}
        DISCOVERY_MODE PRE_TEST
        PROPERTIES
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
    add_dependencies(check_all ${name} taurus-cli)
endfunction()

taurus_add_test(test_smoke      smoke/test_smoke.cpp)
taurus_add_test(test_parser     parser/test_parser.cpp)
taurus_add_test(test_encoding   parser/test_encoding.cpp)
taurus_add_test(test_dom        dom/test_dom.cpp)
taurus_add_test(test_vtable     dom/test_vtable.cpp)
taurus_add_test(test_text_borrowed dom/test_text_borrowed.cpp)
taurus_add_test(test_compact    dom/test_compact.cpp)
taurus_add_test(test_pool       memory/test_pool.cpp)
taurus_add_test(test_xpath      xpath/test_xpath.cpp)
taurus_add_test(test_xpath_conf xpath/test_xpath_conformance.cpp)
taurus_add_test(test_bytecode_vm xpath/test_bytecode_vm.cpp)
taurus_add_test(test_serialize  serializer/test_serialize.cpp)
taurus_add_test(test_c14n       serializer/test_c14n.cpp)
taurus_add_test(test_perf       perf/test_perf.cpp)
taurus_add_test(test_sax        sax/test_sax.cpp)
taurus_add_test(test_streaming  sax/test_streaming.cpp)
taurus_add_test(test_abi        abi/test_header_hygiene.cpp)
taurus_add_test(test_nokogiri_api abi/test_nokogiri_api.cpp)
taurus_add_test(test_prev_sib_c14n abi/test_previous_sibling_and_c14n_ex.cpp)
taurus_add_test(test_ns_mut_create abi/test_namespace_mutation_and_create.cpp)
taurus_add_test(test_dom_mut_bugs abi/test_dom_mutation_bugs.cpp)
taurus_add_test(test_v0_5_14_bugs abi/test_v0_5_14_bugs.cpp)
taurus_add_test(test_dom_deep_copy abi/test_dom_deep_copy.cpp)
taurus_add_test(test_node_get_xpath abi/test_node_get_xpath.cpp)
taurus_add_test(test_parse_fragment abi/test_parse_fragment.cpp)
taurus_add_test(test_doctype_api abi/test_doctype_api.cpp)
taurus_add_test(test_flat_promote_line abi/test_flat_promote_line.cpp)
taurus_add_test(test_flat_doc   flat/test_flat_doc.cpp)
taurus_add_test(test_flat_parser flat/test_flat_parser.cpp)
taurus_add_test(test_flat_promote flat/test_flat_promote.cpp)
taurus_add_test(test_flat_fast   flat/test_flat_fast.cpp)
taurus_add_test(test_flat_ns_promote flat/test_flat_namespace_promote.cpp)
taurus_add_test(test_flat_xpath   flat/test_flat_xpath.cpp)

# Shared-library export check (TODO 122).  Runs the bash script that
# builds a one-off libtaurus.dylib / .so and asserts SAX + DOM + XPath
# symbols appear in the export table.  Catches missing TAURUS_API
# annotations that would silently break FFI bindings.
add_test(
    NAME SymbolExportCheck
    COMMAND bash ${PROJECT_SOURCE_DIR}/scripts/check_shared_exports.sh
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
)
taurus_add_test(test_dtd        dtd/test_dtd_validate.cpp)
taurus_add_test(test_xinclude   xinclude/test_xinclude.cpp)
taurus_add_cli_test(test_cli    cli/test_cli.cpp)

# libFuzzer harness (TODO 40) — built only when TAURUS_ENABLE_FUZZING=ON.
# Not a ctest target; invoked manually with corpus.
if(TAURUS_ENABLE_FUZZING)
    add_executable(fuzz_parse ${CMAKE_CURRENT_SOURCE_DIR}/fuzz/fuzz_parse.c)
    target_link_libraries(fuzz_parse PRIVATE taurus)
    target_include_directories(fuzz_parse PRIVATE
        ${PROJECT_SOURCE_DIR}/src/include)
endif()
