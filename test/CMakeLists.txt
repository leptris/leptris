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

# Native builds discover the gtest cases at BUILD time so the list is
# baked into CTestTestfile and `ctest --parallel` can schedule the
# whole suite. PRE_TEST expands the list while ctest runs, which only
# works serially — under -j the _NOT_BUILT placeholders execute. Cross
# builds must keep PRE_TEST: their binaries cannot run on the host.
if(CMAKE_CROSSCOMPILING)
    set(LEPTRIS_GTEST_DISCOVERY_MODE PRE_TEST)
else()
    set(LEPTRIS_GTEST_DISCOVERY_MODE POST_BUILD)
endif()

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
        target_link_libraries(${name} PRIVATE leptris_objects GTest::gtest_main)
    endif()
    target_include_directories(${name} PRIVATE
        ${PROJECT_SOURCE_DIR}/src/include
        # Pool / StringView internals — used by memory specs.
        ${PROJECT_SOURCE_DIR}/src/leptris/memory
        ${PROJECT_SOURCE_DIR}/src/leptris/common
        # DOM internals — used by vtable specs.
        ${PROJECT_SOURCE_DIR}/src/leptris/dom
        # RNG internals — pattern-IR specs (#878).
        ${PROJECT_SOURCE_DIR}/src/leptris/rng
        # Flat doc internals — used by flat parse specs (TODO 139).
        ${PROJECT_SOURCE_DIR}/src/leptris/flat)
    target_compile_features(${name} PRIVATE cxx_std_11)
    # Timing specs measure ratios; concurrent processes steal cache
    # and skew short measurements — run test_perf's cases serially.
    if(name STREQUAL "test_perf")
        gtest_discover_tests(${name}
                             DISCOVERY_MODE ${LEPTRIS_GTEST_DISCOVERY_MODE}
                             DISCOVERY_TIMEOUT 60
                             PROPERTIES RUN_SERIAL TRUE)
    else()
        gtest_discover_tests(${name}
                             DISCOVERY_MODE ${LEPTRIS_GTEST_DISCOVERY_MODE}
                             DISCOVERY_TIMEOUT 60)
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
    # WORKING_DIRECTORY is a first-class keyword in the modern
    # gtest_discover_tests signature. Passing it inside PROPERTIES
    # made the parser eat RUN_SERIAL as an unparsed argument, so the
    # CLI cases were never actually serialized (ctest -j4 then races
    # the shared fixture cwd — the schematron/rng flakes).
    gtest_discover_tests(${name}
        DISCOVERY_MODE ${LEPTRIS_GTEST_DISCOVERY_MODE}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        PROPERTIES RUN_SERIAL TRUE
    )
    add_dependencies(check_all ${name} leptris-cli)
endfunction()

leptris_add_test(test_smoke      smoke/test_smoke.cpp)
leptris_add_test(test_diff       diff/test_diff.cpp)
leptris_add_test(test_sch        sch/test_schematron.cpp)
leptris_add_test(test_sch_corpus  sch/test_schematron_corpus.cpp)
leptris_add_test(test_qt3          xquery/test_qt3.cpp)
target_compile_definitions(test_qt3 PRIVATE
    LEPTRIS_QT3_DIR="${CMAKE_CURRENT_SOURCE_DIR}/xquery/qt3")
# leptris_static aggregates leptris_objects as SOURCES, so the
# objects' INTERFACE compile definition does not flow through to
# test consumers — mirror the capability flag the runner's corpus
# counts branch on.
if(LEPTRIS_ENABLE_DUCET)
    leptris_add_test(test_collation xpath/test_collation.cpp)
endif()

if(LEPTRIS_ENABLE_UTF8PROC)
    target_compile_definitions(test_qt3 PRIVATE LEPTRIS_HAS_UTF8PROC=1)
endif()
target_compile_definitions(test_sch_corpus PRIVATE
    LEPTRIS_SCH_CASES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/sch/conformance-cases")
leptris_add_test(test_parser     parser/test_parser.cpp)
leptris_add_test(test_large_docs parser/test_large_documents.cpp)
leptris_add_test(test_encoding   parser/test_encoding.cpp)
leptris_add_test(test_utf8_simd  parser/test_utf8_simd.cpp)
leptris_add_test(test_entity_decode parser/test_entity_decode.cpp)
leptris_add_test(test_dom        dom/test_dom.cpp)
leptris_add_test(test_element_children dom/test_element_children.cpp)
leptris_add_test(test_entity_refs dom/test_entity_refs.cpp)
leptris_add_test(test_namespace_cache dom/test_namespace_cache.cpp)
leptris_add_test(test_map_lifecycle dom/test_map_lifecycle.cpp)
leptris_add_test(test_simd_count parser/test_simd_count.cpp)
leptris_add_test(test_ws_shape    parser/test_whitespace_shape.cpp)
leptris_add_test(test_descriptor abi/test_descriptor.cpp)
leptris_add_test(test_capabilities abi/test_capabilities.cpp)
# The capability descriptor's build-conditional bits follow the
# engine's PUBLIC macros, which do not flow through the object
# aggregation (same mirror as test_qt3 below).
if(LEPTRIS_ENABLE_DUCET)
    target_compile_definitions(test_capabilities PRIVATE LEPTRIS_HAS_DUCET=1)
endif()
if(LEPTRIS_ENABLE_UTF8PROC)
    target_compile_definitions(test_capabilities PRIVATE LEPTRIS_HAS_UTF8PROC=1)
endif()
if(LEPTRIS_ENABLE_ICONV)
    target_compile_definitions(test_capabilities PRIVATE LEPTRIS_HAS_ICONV=1)
endif()
if(CMAKE_SYSTEM_PROCESSOR MATCHES "(x86_64|AMD64|i[3-6]86)")
    target_compile_definitions(test_capabilities PRIVATE LEPTRIS_HAS_AVX2_BUILD=1)
endif()
leptris_add_test(test_document_children dom/test_document_children.cpp)
leptris_add_test(test_vtable     dom/test_vtable.cpp)
leptris_add_test(test_node_surface_parity dom/test_node_surface_parity.cpp)
leptris_add_test(test_text_borrowed dom/test_text_borrowed.cpp)
leptris_add_test(test_compact    dom/test_compact.cpp)
leptris_add_test(test_digest     dom/test_digest.cpp)
leptris_add_test(test_intern_names dom/test_intern_names.cpp)
leptris_add_test(test_mut_append_contracts dom/test_mut_append_contracts.cpp)
leptris_add_test(test_rng        rng/test_rng.cpp)
leptris_add_test(test_xsd_compile xsd/test_xsd_compile.cpp)
target_compile_definitions(test_xsd_compile PRIVATE
    LEPTRIS_XSD_CASES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/xsd/libxml2-cases")
leptris_add_test(test_rs35_regression xsd/test_rs35_regression.cpp)
target_compile_definitions(test_rs35_regression PRIVATE
    LEPTRIS_XSD_CASES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/xsd/libxml2-cases")
leptris_add_test(test_i1957_1958 xsd/test_i1957_1958.cpp)
target_compile_definitions(test_i1957_1958 PRIVATE
    LEPTRIS_XSD_CASES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/xsd/libxml2-cases")
# #2148: the XSD 1.1 baseline lane — oracle is the pinned w3c/xsdtests
# suite metadata (see xsd/xsd11-cases/MANIFEST.tsv header).
leptris_add_test(test_xsd11_conformance xsd/test_xsd11_conformance.cpp)
target_compile_definitions(test_xsd11_conformance PRIVATE
    LEPTRIS_XSD11_CASES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/xsd/xsd11-cases")
# #2149: conditional type assignment (xs:alternative) spec tests.
leptris_add_test(test_xsd11_cta xsd/test_xsd11_cta.cpp)
target_compile_definitions(test_xsd11_cta PRIVATE
    LEPTRIS_XSD11_CTA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/xsd/xsd11-cta")
# #2184: the alternative-table compile constraints (type
# substitutability, XPath-valid tests, table equality, src-ct.1,
# {inheritable}) — the cta schema-error class; fixtures ride the
# pinned xsd11-cases corpus.
leptris_add_test(test_xsd11_ctatable xsd/test_xsd11_ctatable.cpp)
target_compile_definitions(test_xsd11_ctatable PRIVATE
    LEPTRIS_XSD11_CASES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/xsd/xsd11-cases")
# leptris-ruby#679: targetNamespace qualification on validated
# documents (root resolution + attribute form).
leptris_add_test(test_xsd_ns_qualification xsd/test_xsd_ns_qualification.cpp)
target_compile_definitions(test_xsd_ns_qualification PRIVATE
    LEPTRIS_XSD_NS_QUAL_DIR="${CMAKE_CURRENT_SOURCE_DIR}/xsd/ns-qual")
# #2154: xs:assertion facet spec tests.
leptris_add_test(test_xsd11_assertions xsd/test_xsd11_assertions.cpp)
target_compile_definitions(test_xsd11_assertions PRIVATE
    LEPTRIS_XSD11_ASSERTIONS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/xsd/xsd11-assertions")
# #2183: the assertion-facet XQuery-semantics residue (absent focus,
# fn:doc base resolution, error mapping, subtree rooting, carriers,
# castable-as).
leptris_add_test(test_xsd11_assert_sem xsd/test_xsd11_assert_sem.cpp)
target_compile_definitions(test_xsd11_assert_sem PRIVATE
    LEPTRIS_XSD11_ASSERT_SEM_DIR="${CMAKE_CURRENT_SOURCE_DIR}/xsd/xsd11-assert-sem")
# #2155: facet applicability on the 1.1-only datatypes.
leptris_add_test(test_xsd11_datatypes xsd/test_xsd11_datatypes.cpp)
target_compile_definitions(test_xsd11_datatypes PRIVATE
    LEPTRIS_XSD11_DATATYPES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/xsd/xsd11-datatypes")
# #2156: the identity-constraint 1.1 extensions (subset-restricted
# selector/field XPaths, schema-side ns resolution, @ref reuse,
# vacuous tables).
leptris_add_test(test_xsd11_ic xsd/test_xsd11_ic.cpp)
target_compile_definitions(test_xsd11_ic PRIVATE
    LEPTRIS_XSD11_IC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/xsd/xsd11-ic"
    LEPTRIS_XSD11_CASES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/xsd/xsd11-cases")
# #2150: xs:override (XSD 1.1 §4.3) spec tests.
leptris_add_test(test_xsd11_override xsd/test_xsd11_override.cpp)
target_compile_definitions(test_xsd11_override PRIVATE
    LEPTRIS_XSD11_OVERRIDE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/xsd/xsd11-override"
    LEPTRIS_XSD11_CASES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/xsd/xsd11-cases")
# #2151: xs:openContent / xs:defaultOpenContent (XSD 1.1).
leptris_add_test(test_xsd11_opencontent xsd/test_xsd11_opencontent.cpp)
target_compile_definitions(test_xsd11_opencontent PRIVATE
    LEPTRIS_XSD11_OPENCONTENT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/xsd/xsd11-opencontent")
# #2182: default attribute groups (XSD 1.1 §3.4.2.5) — the suite
# fixtures for the family ride the pinned xsd11-cases corpus.
leptris_add_test(test_xsd11_defattr xsd/test_xsd11_defattr.cpp)
target_compile_definitions(test_xsd11_defattr PRIVATE
    LEPTRIS_XSD11_DEFATTR_DIR="${CMAKE_CURRENT_SOURCE_DIR}/xsd/xsd11-defattr"
    LEPTRIS_XSD11_CASES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/xsd/xsd11-cases")
# #2184: the wildcard negative namespace/name sets (@notNamespace,
# @notQName resolution, gathered-intersection / derived-union algebra,
# the src-wildcard compile sweep, ##definedSibling) — fixtures ride
# the pinned xsd11-cases corpus.
leptris_add_test(test_xsd11_wildcard xsd/test_xsd11_wildcard.cpp)
target_compile_definitions(test_xsd11_wildcard PRIVATE
    LEPTRIS_XSD11_CASES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/xsd/xsd11-cases")
# #2184: attribute uses referencing the built-in xsi declarations
# (the defaulted-xsi:type class) — the saxon Complex shapes ride the
# pinned xsd11-cases corpus.
leptris_add_test(test_xsd11_xitype xsd/test_xsd11_xitype.cpp)
target_compile_definitions(test_xsd11_xitype PRIVATE
    LEPTRIS_XSD11_CASES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/xsd/xsd11-cases")
# #2184: relaxed xs:all (XSD 1.1 §3.8.3) — order-free per-child
# counting, wildcard/group children, all-to-all extension merge and
# restriction; the suite fixtures ride the pinned corpus.
leptris_add_test(test_xsd11_all xsd/test_xsd11_all.cpp)
target_compile_definitions(test_xsd11_all PRIVATE
    LEPTRIS_XSD11_ALL_DIR="${CMAKE_CURRENT_SOURCE_DIR}/xsd/xsd11-all"
    LEPTRIS_XSD11_CASES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/xsd/xsd11-cases")
# #2184: the ID bookkeeping wave — the 1.1 one-ID relaxation (the
# declared multi-ID table is 1.1 evidence) and the ID/IDREF register +
# resolve integrity; the saxon Id / ibm S3_3_4 fixtures ride the
# pinned xsd11-cases corpus.
leptris_add_test(test_xsd11_multid xsd/test_xsd11_multid.cpp)
target_compile_definitions(test_xsd11_multid PRIVATE
    LEPTRIS_XSD11_CASES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/xsd/xsd11-cases")
target_compile_definitions(test_rng PRIVATE
    LEPTRIS_RNG_INCLUDE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/rng/include-cases")
leptris_add_test(test_rng_corpus rng/test_rng_corpus.cpp)
target_compile_definitions(test_rng_corpus PRIVATE
    LEPTRIS_RNG_CASES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/rng/jing-cases")
leptris_add_test(test_rng_corpus_msg rng/test_rng_corpus_msg.cpp)
target_compile_definitions(test_rng_corpus_msg PRIVATE
    LEPTRIS_RNG_CASES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/rng/jing-cases")
if(LEPTRIS_ENABLE_UTF8PROC)
    # the regex suites gate their unicode-escape assertions on the
    # same macro the ENGINE was built with — a mismatch silently
    # compiles out the assertions (the gtest then "passes" while
    # the engine behavior goes unverified).
    target_compile_definitions(test_rng PRIVATE LEPTRIS_HAS_UTF8PROC=1)
    target_compile_definitions(test_rng_corpus
        PRIVATE LEPTRIS_HAS_UTF8PROC=1)
    target_compile_definitions(test_rng_corpus_msg
        PRIVATE LEPTRIS_HAS_UTF8PROC=1)
endif()
leptris_add_test(test_simd_text  common/test_simd_text.cpp)
leptris_add_test(test_simd_spans common/test_simd_spans.cpp)
leptris_add_test(test_pool       memory/test_pool.cpp)
leptris_add_test(test_oom        memory/test_oom_injection.cpp)
leptris_add_test(test_arena      memory/test_arena.cpp)
leptris_add_test(test_doc_block memory/test_doc_block.cpp)
leptris_add_test(test_interleaved parser/test_interleaved.cpp)
leptris_add_test(test_pool_arena memory/test_pool_arena.cpp)
leptris_add_test(test_mut_recycle memory/test_mut_recycle.cpp)
leptris_add_test(test_xpath      xpath/test_xpath.cpp)
leptris_add_test(test_exslt       xpath/test_exslt.cpp)
leptris_add_test(test_compiled   xpath/test_compiled.cpp)
leptris_add_test(test_xpath_conf xpath/test_xpath_conformance.cpp)
leptris_add_test(test_bytecode_vm xpath/test_bytecode_vm.cpp)
leptris_add_test(test_xslt        xpath/test_xslt.cpp)
if(LEPTRIS_ENABLE_DUCET)
    target_compile_definitions(test_xslt PRIVATE LEPTRIS_HAS_DUCET=1)
    if(TARGET test_qt3)
        target_compile_definitions(test_qt3 PRIVATE LEPTRIS_HAS_DUCET=1)
    endif()
    if(TARGET test_xquery)
        target_compile_definitions(test_xquery PRIVATE LEPTRIS_HAS_DUCET=1)
    endif()
endif()
leptris_add_test(test_xquery      xquery/test_xquery.cpp)
leptris_add_test(test_html        html/test_html.cpp)
leptris_add_test(test_html_amp    html/test_html_amplification.cpp)
leptris_add_test(test_html_parse_errors html/test_html_parse_errors.cpp)
leptris_add_test(test_html_builder html/test_html_builder.cpp)
leptris_add_test(test_html_borrowed_names html/test_html_borrowed_names.cpp)
leptris_add_test(test_html5lib     html/test_html5lib.cpp)
file(GLOB html5lib_dats
     "${CMAKE_CURRENT_SOURCE_DIR}/html/html5lib-tests/tree-construction/*.dat")
list(SORT html5lib_dats)
set(HTML5LIB_DAT_LIST "")
foreach(f ${html5lib_dats})
    get_filename_component(basename "${f}" NAME)
    list(APPEND HTML5LIB_DAT_LIST "\"${basename}\"")
endforeach()
string(REPLACE ";" ",\n    " HTML5LIB_DAT_INITIALIZER "${HTML5LIB_DAT_LIST}")
configure_file(html/html5lib_cases.h.in html/html5lib_cases.h @ONLY)
target_compile_definitions(test_html5lib PRIVATE
    LEPTRIS_HTML5LIB_DIR="${CMAKE_CURRENT_SOURCE_DIR}/html/html5lib-tests/tree-construction")
target_include_directories(test_html5lib PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/html)
target_compile_definitions(test_xquery PRIVATE
    LEPTRIS_XQUERY_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/test/xquery")

# The libxslt general suite (test/xslt/libxslt_suite_general) as
# behavioral specs — data files stay in the source tree.
leptris_add_test(test_libxslt_suite xslt/test_libxslt_suite.cpp)
target_compile_definitions(test_libxslt_suite PRIVATE
    LEPTRIS_XSLT_SUITE_DIR="${PROJECT_SOURCE_DIR}/test/xslt/libxslt_suite_general"
    LEPTRIS_XSLT_OPEN_LIST="${PROJECT_SOURCE_DIR}/test/xslt/open_cases.txt")
leptris_add_test(test_serialize  serializer/test_serialize.cpp)
leptris_add_test(test_c14n       serializer/test_c14n.cpp)
leptris_add_test(test_perf       perf/test_perf.cpp)
leptris_add_test(test_sax        sax/test_sax.cpp)
leptris_add_test(test_pull       sax/test_pull.cpp)
leptris_add_test(test_pull_batch sax/test_pull_batch.cpp)
leptris_add_test(test_sax_records sax/test_records.cpp)
leptris_add_test(test_recorder   sax/test_recorder.cpp)
leptris_add_test(test_iterparse  sax/test_iterparse.cpp)
leptris_add_test(test_streaming  sax/test_streaming.cpp)
leptris_add_test(test_abi        abi/test_header_hygiene.cpp)
leptris_add_test(test_version_lockstep abi/test_version_lockstep.cpp)
# The lockstep spec reads the repo's own version surfaces (CMakeLists,
# leptris.h, core.c) — hand it the repo root.
target_compile_definitions(test_version_lockstep PRIVATE
    LEPTRIS_REPO_ROOT="${PROJECT_SOURCE_DIR}")
leptris_add_test(test_public_surface abi/test_public_surface.cpp)
leptris_add_test(test_serialize_abi abi/test_serialize_abi.cpp)
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
# TODO.concurrency/08: thread-spawning spec needs pthreads on
# POSIX; the helper above cannot express that.
find_package(Threads REQUIRED)
leptris_add_test(test_concurrency concurrency/test_concurrency.cpp)
target_link_libraries(test_concurrency PRIVATE Threads::Threads)
# leptris-ruby#321: cross-thread document death vs the root-doc map.
leptris_add_test(test_root_doc_cross_thread concurrency/test_root_doc_cross_thread.cpp)
target_link_libraries(test_root_doc_cross_thread PRIVATE Threads::Threads)


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
leptris_add_test(test_dtd_w3c    dtd/test_w3c_conformance.cpp)
leptris_add_test(test_xinclude   xinclude/test_xinclude.cpp)
leptris_add_test(test_xinclude_fb xinclude/test_xinclude_fallback_elem.cpp)
leptris_add_cli_test(test_cli    cli/test_cli.cpp)

# FFI mirror drift gate (architecture review D): the Ruby/Python
# binding mirrors must match the public headers (no phantom symbols,
# no arity drift, core surface present). Python-based; skipped when
# no interpreter is found.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_FOUND)
    add_test(NAME ffi_mirror_drift
        COMMAND ${Python3_EXECUTABLE}
                ${PROJECT_SOURCE_DIR}/scripts/check_ffi_mirrors.py
                ${PROJECT_SOURCE_DIR})
endif()

# Export-surface gate (TODO.concurrency/02): every symbol exported
# from the shared library must be declared in src/include/ and vice
# versa. Skips (exit 0) when no shared library was built.
if(Python3_FOUND)
    add_test(NAME export_surface_drift
        COMMAND ${Python3_EXECUTABLE}
                ${PROJECT_SOURCE_DIR}/scripts/check_export_surface.py
                ${CMAKE_BINARY_DIR}/src/libleptris.dylib
                ${PROJECT_SOURCE_DIR})
    set_tests_properties(export_surface_drift PROPERTIES
        ENVIRONMENT "PYTHONPATH=")
endif()

# libFuzzer harnesses — built only when LEPTRIS_ENABLE_FUZZING=ON.
# Not ctest targets; invoked manually with a corpus (or by the
# nightly fuzz workflow, which invokes them at the build root —
# subdirectory targets land under build/test/ by default).
if(LEPTRIS_ENABLE_FUZZING)
    foreach(target fuzz_parse fuzz_mutate fuzz_xpath fuzz_xslt fuzz_html)
        add_executable(${target}
            ${CMAKE_CURRENT_SOURCE_DIR}/fuzz/${target}.c)
        target_link_libraries(${target} PRIVATE leptris)
        target_include_directories(${target} PRIVATE
            ${PROJECT_SOURCE_DIR}/src/include)
        set_target_properties(${target} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
    endforeach()
endif()
