# Taurus vs libxml2 / pugixml benchmark suite.
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
add_library(taurus_bench_common STATIC
    common/benchmark.c
    common/test_data.c
)
target_include_directories(taurus_bench_common PUBLIC common)
target_link_libraries(taurus_bench_common PUBLIC taurus)

# Helper: declare a benchmark target with the standard include + link setup.
function(taurus_add_benchmark target_name)
    cmake_parse_arguments(ARG "CXX" "" "" ${ARGN})
    add_executable(${target_name} ${ARG_UNPARSED_ARGUMENTS})
    target_include_directories(${target_name} PRIVATE
        ${CMAKE_SOURCE_DIR}/src/include
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_link_libraries(${target_name} PRIVATE taurus_bench_common taurus)
    if(ARG_CXX)
        set_target_properties(${target_name} PROPERTIES
            CXX_STANDARD 11
            CXX_STANDARD_REQUIRED ON)
    endif()
endfunction()

# ----------------------------------------------------------------------------
# DOM benchmarks (taurus vs libxml2)
# ----------------------------------------------------------------------------
if(LIBXML2_AVAILABLE)
    taurus_add_benchmark(bench_dom_taurus      dom/bench_taurus.c)
taurus_add_benchmark(bench_text_borrowed   dom/bench_text_borrowed.c)
    target_compile_definitions(bench_dom_taurus PRIVATE HAVE_LIBXML2=1)
    target_include_directories(bench_dom_taurus PRIVATE ${LIBXML2_INCLUDE_DIR})
    target_link_libraries(bench_dom_taurus PRIVATE ${LIBXML2_LIBRARY})

    taurus_add_benchmark(bench_dom_libxml2     dom/bench_libxml2.c)
    target_include_directories(bench_dom_libxml2 PRIVATE ${LIBXML2_INCLUDE_DIR})
    target_link_libraries(bench_dom_libxml2 PRIVATE ${LIBXML2_LIBRARY})
endif()

# ----------------------------------------------------------------------------
# XPath benchmarks
# ----------------------------------------------------------------------------
if(LIBXML2_AVAILABLE)
    set(_xpath_pairs
        "bench_xpath_taurus|xpath/bench_taurus.c"
        "bench_xpath_libxml2|xpath/bench_libxml2.c"
        "bench_xpath_axes_taurus|xpath/bench_axes.c"
        "bench_xpath_axes_libxml2|xpath/bench_axes_libxml2.c"
        "bench_xpath_functions_taurus|xpath/bench_functions.c"
        "bench_xpath_functions_libxml2|xpath/bench_functions_libxml2.c")
    foreach(pair ${_xpath_pairs})
        string(REPLACE "|" ";" kv "${pair}")
        list(GET kv 0 tgt)
        list(GET kv 1 src)
        taurus_add_benchmark(${tgt} ${src})
        target_include_directories(${tgt} PRIVATE ${LIBXML2_INCLUDE_DIR})
        target_link_libraries(${tgt} PRIVATE ${LIBXML2_LIBRARY})
    endforeach()
endif()

# Diagnostic benchmarks — taurus only, used to isolate per-component
# costs (parse vs eval, cold vs warm cache, setup floor, predicate
# cost, named-attribute mystery, etc). See TODO 123.
taurus_add_benchmark(bench_xpath_diagnostic xpath/bench_diagnostic.c)

# XPath vs pugixml — added in TODO 107 to close the pugixml coverage gap.
if(PUGIXML_AVAILABLE)
    taurus_add_benchmark(bench_xpath_pugixml CXX xpath/bench_pugixml.cpp)
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
    taurus_add_benchmark(benchmark_write CXX comprehensive/benchmark_write.cpp)
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
            benchmark_memory)
        taurus_add_benchmark(${name} CXX comprehensive/${name}.cpp)
        if(PUGIXML_TARGET)
            target_link_libraries(${name} PRIVATE ${PUGIXML_TARGET})
        else()
            target_include_directories(${name} PRIVATE ${PUGIXML_INCLUDE_DIR})
            target_link_libraries(${name} PRIVATE ${PUGIXML_LIBRARY})
        endif()
    endforeach()

    # Newer integrated DOM comparison.
    taurus_add_benchmark(dom_benchmark_v2 CXX dom/dom_benchmark_v2.cpp utils.c)
    if(PUGIXML_TARGET)
        target_link_libraries(dom_benchmark_v2 PRIVATE ${PUGIXML_TARGET})
    else()
        target_include_directories(dom_benchmark_v2 PRIVATE ${PUGIXML_INCLUDE_DIR})
        target_link_libraries(dom_benchmark_v2 PRIVATE ${PUGIXML_LIBRARY})
    endif()
endif()

# ----------------------------------------------------------------------------
# Serialization / modification (taurus only — for self-tracking over time)
# ----------------------------------------------------------------------------
taurus_add_benchmark(bench_serialize        serialization/bench_taurus_serialize.c)
taurus_add_benchmark(bench_modify_taurus    modification/bench_taurus_modify.c)
# bench_node_creation.c uses the deprecated _fast node-create variants that
# TODO 18 collapsed into a single entry point; rewrite before re-enabling.

# ----------------------------------------------------------------------------
# SAX benchmarks (taurus vs libxml2).  TODO 101.
# ----------------------------------------------------------------------------
if(LIBXML2_AVAILABLE)
    taurus_add_benchmark(bench_sax_taurus    sax/bench_taurus.c)
    target_include_directories(bench_sax_taurus PRIVATE ${LIBXML2_INCLUDE_DIR})
    target_link_libraries(bench_sax_taurus PRIVATE ${LIBXML2_LIBRARY})

    taurus_add_benchmark(bench_sax_libxml2   sax/bench_libxml2.c)
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
    message(WARNING "Neither pugixml nor libxml2 found — only taurus self-benchmarks will build.")
endif()
