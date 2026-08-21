# test/CMakeLists.txt — Google Test integration for Leptris.
#
# Each subdirectory under test/ maps to one module under src/leptris/.
# Adding a new test executable = one call to leptris_add_test().

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
    COMMENT "Running all Leptris specs")

# leptris_add_test(<name> <sources...>)
#
# Creates an executable, links it to libleptris + gtest_main, registers
# it with ctest via gtest_discover_tests, and adds it to check_all.
function(leptris_add_test name)
    add_executable(${name} ${ARGN})
    # Specs exercise internal symbols (pool internals, vtables,
    # compact pointers) that the shared library does not export —
    # always link the static objects.
    if(TARGET leptris_static)
        target_link_libraries(${name} PRIVATE leptris_static GTest::gtest_main)
    else()
        target_link_libraries(${name} PRIVATE leptris GTest::gtest_main)
    endif()
    target_include_directories(${name} PRIVATE
        ${PROJECT_SOURCE_DIR}/src/include
        # Pool / StringView internals — used by memory specs.
        ${PROJECT_SOURCE_DIR}/src/leptris/memory
        ${PROJECT_SOURCE_DIR}/src/leptris/common
        # DOM internals — used by vtable specs.
        ${PROJECT_SOURCE_DIR}/src/leptris/dom
        # Flat doc internals — used by flat parse specs (TODO 139).
        ${PROJECT_SOURCE_DIR}/src/leptris/flat)
    target_compile_features(${name} PRIVATE cxx_std_11)
    # Timing specs measure ratios; concurrent processes steal cache
    # and skew short measurements — run test_perf's cases serially.
    if(name STREQUAL "test_perf")
        gtest_discover_tests(${name} DISCOVERY_MODE PRE_TEST
                             PROPERTIES RUN_SERIAL TRUE)
    else()
        gtest_discover_tests(${name} DISCOVERY_MODE PRE_TEST)
    endif()
    add_dependencies(check_all ${name})
endfunction()

# Specialization for CLI specs, which invoke the built binary via
# subprocess and need to know its path.
function(leptris_add_cli_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE GTest::gtest_main)
    target_compile_features(${name} PRIVATE cxx_std_11)
    target_compile_definitions(${name} PRIVATE
        LEPTRIS_CLI_BIN="${CMAKE_BINARY_DIR}/cli/leptris"
        LEPTRIS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    gtest_discover_tests(${name}
        DISCOVERY_MODE PRE_TEST
        PROPERTIES
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        # CLI cases spawn subprocesses; fork/exec contention under
        # parallel ctest flakes them — run serially.
        RUN_SERIAL TRUE
    )
    add_dependencies(check_all ${name} leptris-cli)
endfunction()

leptris_add_test(test_smoke      smoke/test_smoke.cpp)
leptris_add_test(test_parser     parser/test_parser.cpp)
leptris_add_test(test_large_docs parser/test_large_documents.cpp)
leptris_add_test(test_encoding   parser/test_encoding.cpp)
leptris_add_test(test_dom        dom/test_dom.cpp)
leptris_add_test(test_vtable     dom/test_vtable.cpp)
leptris_add_test(test_text_borrowed dom/test_text_borrowed.cpp)
leptris_add_test(test_compact    dom/test_compact.cpp)
leptris_add_test(test_simd_text  common/test_simd_text.cpp)
leptris_add_test(test_simd_spans common/test_simd_spans.cpp)
leptris_add_test(test_pool       memory/test_pool.cpp)
leptris_add_test(test_oom        memory/test_oom_injection.cpp)
leptris_add_test(test_arena      memory/test_arena.cpp)
leptris_add_test(test_pool_arena memory/test_pool_arena.cpp)
leptris_add_test(test_xpath      xpath/test_xpath.cpp)
leptris_add_test(test_xpath_conf xpath/test_xpath_conformance.cpp)
leptris_add_test(test_bytecode_vm xpath/test_bytecode_vm.cpp)
leptris_add_test(test_serialize  serializer/test_serialize.cpp)
leptris_add_test(test_c14n       serializer/test_c14n.cpp)
leptris_add_test(test_perf       perf/test_perf.cpp)
leptris_add_test(test_sax        sax/test_sax.cpp)
leptris_add_test(test_streaming  sax/test_streaming.cpp)
leptris_add_test(test_abi        abi/test_header_hygiene.cpp)
leptris_add_test(test_nokogiri_api abi/test_nokogiri_api.cpp)
leptris_add_test(test_prev_sib_c14n abi/test_previous_sibling_and_c14n_ex.cpp)
leptris_add_test(test_ns_mut_create abi/test_namespace_mutation_and_create.cpp)
leptris_add_test(test_dom_mut_bugs abi/test_dom_mutation_bugs.cpp)
leptris_add_test(test_v0_5_14_bugs abi/test_v0_5_14_bugs.cpp)
leptris_add_test(test_dom_deep_copy abi/test_dom_deep_copy.cpp)
leptris_add_test(test_node_get_xpath abi/test_node_get_xpath.cpp)
leptris_add_test(test_parse_fragment abi/test_parse_fragment.cpp)
leptris_add_test(test_doctype_api abi/test_doctype_api.cpp)
leptris_add_test(test_xpath_custom_fns abi/test_xpath_custom_fns.cpp)

# High-document-count stress test (issues #256, #261). Parses 5,000
# docs in batches of 500, verifying tree integrity on each.
leptris_add_test(test_stress_high_doc_count perf/test_stress_high_doc_count.cpp)

# FlatDoc + lazy-promote tests removed — direct_parse is the sole
# parser and builds the LeptrisElement tree eagerly. The flat/
# subsystem (TODO 139) has been deleted.

# Shared-library export check (TODO 122).  Runs the bash script that
# builds a one-off libleptris.dylib / .so and asserts SAX + DOM + XPath
# symbols appear in the export table.  Catches missing LEPTRIS_API
# annotations that would silently break FFI bindings.
add_test(
    NAME SymbolExportCheck
    COMMAND bash ${PROJECT_SOURCE_DIR}/scripts/check_shared_exports.sh
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
)
leptris_add_test(test_dtd        dtd/test_dtd_validate.cpp)
leptris_add_test(test_xinclude   xinclude/test_xinclude.cpp)
leptris_add_cli_test(test_cli    cli/test_cli.cpp)

# libFuzzer harness (TODO 40) — built only when LEPTRIS_ENABLE_FUZZING=ON.
# Not a ctest target; invoked manually with corpus.
if(LEPTRIS_ENABLE_FUZZING)
    add_executable(fuzz_parse ${CMAKE_CURRENT_SOURCE_DIR}/fuzz/fuzz_parse.c)
    target_link_libraries(fuzz_parse PRIVATE leptris)
    target_include_directories(fuzz_parse PRIVATE
        ${PROJECT_SOURCE_DIR}/src/include)
endif()
