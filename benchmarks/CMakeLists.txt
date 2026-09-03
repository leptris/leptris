# Leptris vs libxml2 / pugixml benchmark suite.
#
# Re-organized for the on-disk layout: dom/, xpath/, comprehensive/,
# serialization/, modification/ each contribute their own targets.
# The shared harness in common/ provides timing, CPU, and RSS reporting.
#
# Resource tracking (CPU time + peak RSS) is wired through every
# benchmark via the common harness; see TODO 101.

cmake_minimum_required(VERSION 3.20)

# ----------------------------------------------------------------------------
# Optional dependencies: pugixml (DOM competitor) and libxml2 (XPath/SAX competitor)
# ----------------------------------------------------------------------------
# Modern pugixml config exports the pugixml::pugixml imported target.
# Fall back to find_path/find_library for older installs / hand-built trees.
find_package(pugixml QUIET)
if(TARGET pugixml::pugixml)
    set(PUGIXML_AVAILABLE TRUE)
    set(PUGIXML_TARGET pugixml::pugixml)
else()
    find_path(PUGIXML_INCLUDE_DIR NAMES pugixml.hpp PATH_SUFFIXES pugixml src)
    find_library(PUGIXML_LIBRARY  NAMES pugixml)
    if(PUGIXML_INCLUDE_DIR AND PUGIXML_LIBRARY AND NOT PUGIXML_INCLUDE_DIR MATCHES "-NOTFOUND")
        set(PUGIXML_AVAILABLE TRUE)
        set(PUGIXML_TARGET "")
    else()
        message(STATUS "pugixml not found — DOM comparison benchmarks skipped.")
        set(PUGIXML_AVAILABLE FALSE)
    endif()
endif()

find_package(LibXml2 QUIET)
if(LIBXML2_FOUND)
    set(LIBXML2_AVAILABLE TRUE)
    set(LIBXML2_INCLUDE_DIR ${LIBXML2_INCLUDE_DIR})
    set(LIBXML2_LIBRARY ${LIBXML2_LIBRARIES})
else()
    find_path(LIBXML2_INCLUDE_DIR NAMES libxml/parser.h PATH_SUFFIXES libxml2)
    find_library(LIBXML2_LIBRARY  NAMES xml2)
    if(LIBXML2_INCLUDE_DIR AND LIBXML2_LIBRARY AND NOT LIBXML2_INCLUDE_DIR MATCHES "-NOTFOUND")
        set(LIBXML2_AVAILABLE TRUE)
    else()
        message(STATUS "libxml2 not found — XPath/SAX comparison benchmarks skipped.")
        set(LIBXML2_AVAILABLE FALSE)
    endif()
endif()

# ----------------------------------------------------------------------------
# Shared harness
# ----------------------------------------------------------------------------
add_library(leptris_bench_common STATIC
    common/benchmark.c
    common/test_data.c
)
target_include_directories(leptris_bench_common PUBLIC common)
target_link_libraries(leptris_bench_common PUBLIC leptris)

# Helper: declare a benchmark target with the standard include + link setup.
function(leptris_add_benchmark target_name)
    cmake_parse_arguments(ARG "CXX" "" "" ${ARGN})
    add_executable(${target_name} ${ARG_UNPARSED_ARGUMENTS})
    target_include_directories(${target_name} PRIVATE
        ${CMAKE_SOURCE_DIR}/src/include
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_link_libraries(${target_name} PRIVATE leptris_bench_common leptris)
    if(ARG_CXX)
        set_target_properties(${target_name} PROPERTIES
            CXX_STANDARD 11
            CXX_STANDARD_REQUIRED ON)
    endif()
endfunction()

# ----------------------------------------------------------------------------
# DOM benchmarks (leptris vs libxml2)
# ----------------------------------------------------------------------------
if(LIBXML2_AVAILABLE)
    leptris_add_benchmark(bench_dom_leptris      dom/bench_leptris.c)
leptris_add_benchmark(bench_text_borrowed   dom/bench_text_borrowed.c)
    target_compile_definitions(bench_dom_leptris PRIVATE HAVE_LIBXML2=1)
    target_include_directories(bench_dom_leptris PRIVATE ${LIBXML2_INCLUDE_DIR})
    target_link_libraries(bench_dom_leptris PRIVATE ${LIBXML2_LIBRARY})

    leptris_add_benchmark(bench_dom_libxml2     dom/bench_libxml2.c)
    target_include_directories(bench_dom_libxml2 PRIVATE ${LIBXML2_INCLUDE_DIR})
    target_link_libraries(bench_dom_libxml2 PRIVATE ${LIBXML2_LIBRARY})

    # SAX-mode comparison: leptris events vs libxml2 SAX2 (pugixml has
    # no SAX interface). No-op handlers both sides — pure scan +
    # dispatch cost, no tree construction.
    leptris_add_benchmark(benchmark_sax comprehensive/benchmark_sax.c)
    target_include_directories(benchmark_sax PRIVATE ${LIBXML2_INCLUDE_DIR})
    target_link_libraries(benchmark_sax PRIVATE ${LIBXML2_LIBRARY})
endif()

# Full-feature matrix: leptris vs pugixml vs libxml2 (YAML + HTML report).
if(PUGIXML_AVAILABLE AND LIBXML2_AVAILABLE)
    leptris_add_benchmark(bench_matrix CXX matrix/bench_matrix.cpp)
    target_compile_definitions(bench_matrix PRIVATE
        HAVE_PUGIXML=1 HAVE_LIBXML2=1)
    if(PUGIXML_TARGET)
        target_link_libraries(bench_matrix PRIVATE ${PUGIXML_TARGET})
    else()
        target_include_directories(bench_matrix PRIVATE ${PUGIXML_INCLUDE_DIR})
        target_link_libraries(bench_matrix PRIVATE ${PUGIXML_LIBRARY})
    endif()
    target_include_directories(bench_matrix PRIVATE ${LIBXML2_INCLUDE_DIR})
    target_link_libraries(bench_matrix PRIVATE ${LIBXML2_LIBRARY})
elseif(PUGIXML_AVAILABLE)
    leptris_add_benchmark(bench_matrix CXX matrix/bench_matrix.cpp)
    target_compile_definitions(bench_matrix PRIVATE HAVE_PUGIXML=1)
    if(PUGIXML_TARGET)
        target_link_libraries(bench_matrix PRIVATE ${PUGIXML_TARGET})
    else()
        target_include_directories(bench_matrix PRIVATE ${PUGIXML_INCLUDE_DIR})
        target_link_libraries(bench_matrix PRIVATE ${PUGIXML_LIBRARY})
    endif()
endif()

# ----------------------------------------------------------------------------
# XPath benchmarks
# ----------------------------------------------------------------------------
if(LIBXML2_AVAILABLE)
    set(_xpath_pairs
        "bench_xpath_leptris|xpath/bench_leptris.c"
        "bench_xpath_libxml2|xpath/bench_libxml2.c"
        "bench_xpath_axes_leptris|xpath/bench_axes.c"
        "bench_xpath_axes_libxml2|xpath/bench_axes_libxml2.c"
        "bench_xpath_functions_leptris|xpath/bench_functions.c"
        "bench_xpath_functions_libxml2|xpath/bench_functions_libxml2.c"
        "bench_scalar_eval|xpath/bench_scalar_eval.c"
        "bench_scalar_eval_libxml2|xpath/bench_scalar_eval_libxml2.c"
        "bench_xslt_transform|xpath/bench_xslt_transform.c")
    foreach(pair ${_xpath_pairs})
        string(REPLACE "|" ";" kv "${pair}")
        list(GET kv 0 tgt)
        list(GET kv 1 src)
        leptris_add_benchmark(${tgt} ${src})
        target_include_directories(${tgt} PRIVATE ${LIBXML2_INCLUDE_DIR})
        target_link_libraries(${tgt} PRIVATE ${LIBXML2_LIBRARY})
    endforeach()
endif()

# Diagnostic benchmarks — leptris only, used to isolate per-component
# costs (parse vs eval, cold vs warm cache, setup floor, predicate
# cost, named-attribute mystery, etc). See TODO 123.
leptris_add_benchmark(bench_xpath_diagnostic xpath/bench_diagnostic.c)

# XPath vs pugixml — added in TODO 107 to close the pugixml coverage gap.
if(PUGIXML_AVAILABLE)
    leptris_add_benchmark(bench_xpath_pugixml CXX xpath/bench_pugixml.cpp)
    if(PUGIXML_TARGET)
        target_link_libraries(bench_xpath_pugixml PRIVATE ${PUGIXML_TARGET})
    else()
        target_include_directories(bench_xpath_pugixml PRIVATE ${PUGIXML_INCLUDE_DIR})
        target_link_libraries(bench_xpath_pugixml PRIVATE ${PUGIXML_LIBRARY})
    endif()
endif()

# ----------------------------------------------------------------------------
# Comprehensive parse / memory / access benchmarks (C++ — use pugixml)
# ----------------------------------------------------------------------------
if(PUGIXML_AVAILABLE AND LIBXML2_AVAILABLE)
    # benchmark_write needs both pugixml AND libxml2 to compare.
    leptris_add_benchmark(benchmark_write CXX comprehensive/benchmark_write.cpp)
    if(PUGIXML_TARGET)
        target_link_libraries(benchmark_write PRIVATE ${PUGIXML_TARGET})
    else()
        target_include_directories(benchmark_write PRIVATE ${PUGIXML_INCLUDE_DIR})
        target_link_libraries(benchmark_write PRIVATE ${PUGIXML_LIBRARY})
    endif()
    target_include_directories(benchmark_write PRIVATE ${LIBXML2_INCLUDE_DIR})
    target_link_libraries(benchmark_write PRIVATE ${LIBXML2_LIBRARY})
endif()

if(PUGIXML_AVAILABLE)
    foreach(name
            benchmark_parse
            benchmark_first_access
            benchmark_parse_ops
            benchmark_memory
            benchmark_many_attrs
            benchmark_decomp)
        leptris_add_benchmark(${name} CXX comprehensive/${name}.cpp)
        if(PUGIXML_TARGET)
            target_link_libraries(${name} PRIVATE ${PUGIXML_TARGET})
        else()
            target_include_directories(${name} PRIVATE ${PUGIXML_INCLUDE_DIR})
            target_link_libraries(${name} PRIVATE ${PUGIXML_LIBRARY})
        endif()
    endforeach()

    # Newer integrated DOM comparison.
    leptris_add_benchmark(dom_benchmark_v2 CXX dom/dom_benchmark_v2.cpp utils.c)
    if(PUGIXML_TARGET)
        target_link_libraries(dom_benchmark_v2 PRIVATE ${PUGIXML_TARGET})
    else()
        target_include_directories(dom_benchmark_v2 PRIVATE ${PUGIXML_INCLUDE_DIR})
        target_link_libraries(dom_benchmark_v2 PRIVATE ${PUGIXML_LIBRARY})
    endif()
endif()

# ----------------------------------------------------------------------------
# Serialization / modification (leptris only — for self-tracking over time)
# ----------------------------------------------------------------------------
leptris_add_benchmark(bench_serialize        serialization/bench_leptris_serialize.c)
leptris_add_benchmark(bench_modify_leptris    modification/bench_leptris_modify.c)

# XSLT release scorecard (TODO.xslt-full/13, #682): the transform
# fixtures the perf lane gates on — best-of protocol, never mean.
leptris_add_benchmark(bench_xslt_dispatch     xslt/bench_dispatch.c utils.c)
leptris_add_benchmark(bench_xslt_valueof      xslt/bench_valueof.c utils.c)
leptris_add_benchmark(bench_xslt_subtree_copy xslt/bench_subtree_copy.c utils.c)
# bench_node_creation.c uses the deprecated _fast node-create variants that
# TODO 18 collapsed into a single entry point; rewrite before re-enabling.

# ----------------------------------------------------------------------------
# Flat document buffer benchmark (TODO 139 Phase F) — leptris only.
# FlatDoc benchmarks removed — the flat/ subsystem has been deleted.
# direct_parse is the sole parser; use bench_parse for parse benchmarks.

# ----------------------------------------------------------------------------
# SAX benchmarks (leptris vs libxml2).  TODO 101.
# ----------------------------------------------------------------------------
if(LIBXML2_AVAILABLE)
    leptris_add_benchmark(bench_sax_leptris    sax/bench_leptris.c)
    target_include_directories(bench_sax_leptris PRIVATE ${LIBXML2_INCLUDE_DIR})
    target_link_libraries(bench_sax_leptris PRIVATE ${LIBXML2_LIBRARY})

    leptris_add_benchmark(bench_sax_libxml2   sax/bench_libxml2.c)
    target_include_directories(bench_sax_libxml2 PRIVATE ${LIBXML2_INCLUDE_DIR})
    target_link_libraries(bench_sax_libxml2 PRIVATE ${LIBXML2_LIBRARY})
endif()

# ----------------------------------------------------------------------------
# Print which targets will be built so CI logs are debuggable.
# ----------------------------------------------------------------------------
if(PUGIXML_AVAILABLE)
    message(STATUS "  pugixml include: ${PUGIXML_INCLUDE_DIR}")
endif()
if(LIBXML2_AVAILABLE)
    message(STATUS "  libxml2 include: ${LIBXML2_INCLUDE_DIR}")
endif()
if(NOT PUGIXML_AVAILABLE AND NOT LIBXML2_AVAILABLE)
    message(WARNING "Neither pugixml nor libxml2 found — only leptris self-benchmarks will build.")
endif()
