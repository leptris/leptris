# Comprehensive Benchmark Suite
# 7 benchmark files with 57 total tests
# Measures Taurus vs pugixml and libxml2

cmake_minimum_required(VERSION 3.20)

# ============================================================================
# Find dependencies
# ============================================================================

# Find pugixml
set(PUGIXML_EXTERNAL_PATH "/Users/mulgogi/src/external/pugixml")

if(EXISTS "${PUGIXML_EXTERNAL_PATH}/CMakeLists.txt")
    set(PUGIXML_EXTERNAL TRUE)
    set(PUGIXML_INCLUDE_DIR "${PUGIXML_EXTERNAL_PATH}/src")
    set(PUGIXML_LIBRARY "${PUGIXML_EXTERNAL_PATH}/build/libpugixml.a")
else()
    set(PUGIXML_EXTERNAL FALSE)
endif()

find_package(pugixml QUIET)

if(NOT pugixml_FOUND AND NOT PUGIXML_EXTERNAL)
    find_path(PUGIXML_INCLUDE_DIR NAMES pugixml.hpp PATH_SUFFIXES pugixml src)
    find_library(PUGIXML_LIBRARY NAMES pugixml PATH_SUFFIXES build lib)
endif()

if(pugixml_FOUND OR PUGIXML_EXTERNAL OR (PUGIXML_INCLUDE_DIR AND PUGIXML_LIBRARY))
    set(PUGIXML_AVAILABLE TRUE)
    if(pugixml_FOUND AND NOT PUGIXML_INCLUDE_DIR)
        set(PUGIXML_INCLUDE_DIR ${PUGIXML_INCLUDE_DIRS})
        set(PUGIXML_LIBRARY ${PUGIXML_LIBRARIES})
    endif()
    message(STATUS "pugixml found: ${PUGIXML_INCLUDE_DIR}")
else()
    message(WARNING "pugixml not found - some benchmarks will be disabled")
    set(PUGIXML_AVAILABLE FALSE)
endif()

# Find libxml2
find_package(LibXml2 QUIET)
if(LIBXML2_FOUND)
    set(LIBXML2_AVAILABLE TRUE)
    message(STATUS "libxml2 found: ${LIBXML2_INCLUDE_DIR}")
else()
    find_path(LIBXML2_INCLUDE_DIR NAMES libxml/parser.h)
    find_library(LIBXML2_LIBRARY NAMES xml2)
    if(LIBXML2_INCLUDE_DIR AND LIBXML2_LIBRARY)
        set(LIBXML2_AVAILABLE TRUE)
        message(STATUS "libxml2 found: ${LIBXML2_INCLUDE_DIR}")
    else()
        message(WARNING "libxml2 not found - some benchmarks will be disabled")
        set(LIBXML2_AVAILABLE FALSE)
    endif()
endif()

# ============================================================================
# Common sources
# ============================================================================

set(BENCHMARK_COMMON
    ${CMAKE_CURRENT_SOURCE_DIR}/../utils.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/benchmark.c
)

# ============================================================================
# Benchmark 1: XML Parsing (10 tests)
# ============================================================================

if(PUGIXML_AVAILABLE AND LIBXML2_AVAILABLE)
    add_executable(bench_parsing
        bench_parsing.cpp
        ${BENCHMARK_COMMON}
    )

    target_include_directories(bench_parsing PRIVATE
        ${CMAKE_SOURCE_DIR}/src/include
        ${PUGIXML_INCLUDE_DIR}
        ${LIBXML2_INCLUDE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
    )

    target_link_libraries(bench_parsing PRIVATE
        taurus
        ${PUGIXML_LIBRARY}
        ${LIBXML2_LIBRARY}
        z
        m
    )

    set_target_properties(bench_parsing PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED ON
    )
endif()

# ============================================================================
# Benchmark 2: DOM Traversal (5 tests)
# ============================================================================

if(PUGIXML_AVAILABLE)
    add_executable(bench_traversal
        bench_traversal.cpp
        ${BENCHMARK_COMMON}
    )

    target_include_directories(bench_traversal PRIVATE
        ${CMAKE_SOURCE_DIR}/src/include
        ${PUGIXML_INCLUDE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
    )

    target_link_libraries(bench_traversal PRIVATE
        taurus
        ${PUGIXML_LIBRARY}
    )

    set_target_properties(bench_traversal PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED ON
    )
endif()

# ============================================================================
# Benchmark 3: Attribute Access (7 tests)
# ============================================================================

if(PUGIXML_AVAILABLE)
    add_executable(bench_attributes
        bench_attributes.cpp
        ${BENCHMARK_COMMON}
    )

    target_include_directories(bench_attributes PRIVATE
        ${CMAKE_SOURCE_DIR}/src/include
        ${PUGIXML_INCLUDE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
    )

    target_link_libraries(bench_attributes PRIVATE
        taurus
        ${PUGIXML_LIBRARY}
    )

    set_target_properties(bench_attributes PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED ON
    )
endif()

# ============================================================================
# Benchmark 4: DOM Modification (8 tests)
# ============================================================================

if(PUGIXML_AVAILABLE)
    add_executable(bench_modification
        bench_modification.cpp
        ${BENCHMARK_COMMON}
    )

    target_include_directories(bench_modification PRIVATE
        ${CMAKE_SOURCE_DIR}/src/include
        ${PUGIXML_INCLUDE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
    )

    target_link_libraries(bench_modification PRIVATE
        taurus
        ${PUGIXML_LIBRARY}
    )

    set_target_properties(bench_modification PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED ON
    )
endif()

# ============================================================================
# Benchmark 5: XPath All (20 tests)
# ============================================================================

if(LIBXML2_AVAILABLE)
    add_executable(bench_xpath_all
        bench_xpath_all.cpp
        ${BENCHMARK_COMMON}
    )

    target_include_directories(bench_xpath_all PRIVATE
        ${CMAKE_SOURCE_DIR}/src/include
        ${LIBXML2_INCLUDE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
    )

    target_link_libraries(bench_xpath_all PRIVATE
        taurus
        ${LIBXML2_LIBRARY}
        z
        m
    )

    set_target_properties(bench_xpath_all PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED ON
    )
endif()

# ============================================================================
# Benchmark 6: Memory Efficiency (4 tests)
# ============================================================================

if(PUGIXML_AVAILABLE AND LIBXML2_AVAILABLE)
    add_executable(bench_memory
        bench_memory.cpp
        ${BENCHMARK_COMMON}
    )

    target_include_directories(bench_memory PRIVATE
        ${CMAKE_SOURCE_DIR}/src/include
        ${PUGIXML_INCLUDE_DIR}
        ${LIBXML2_INCLUDE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
    )

    target_link_libraries(bench_memory PRIVATE
        taurus
        ${PUGIXML_LIBRARY}
        ${LIBXML2_LIBRARY}
        z
        m
    )

    set_target_properties(bench_memory PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED ON
    )
endif()

# ============================================================================
# Benchmark 7: Real-World Scenarios (4 tests)
# ============================================================================

if(PUGIXML_AVAILABLE)
    add_executable(bench_scenarios
        bench_scenarios.cpp
        ${BENCHMARK_COMMON}
    )

    target_include_directories(bench_scenarios PRIVATE
        ${CMAKE_SOURCE_DIR}/src/include
        ${PUGIXML_INCLUDE_DIR}
        ${LIBXML2_INCLUDE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
    )

    target_link_libraries(bench_scenarios PRIVATE
        taurus
        ${PUGIXML_LIBRARY}
        ${LIBXML2_LIBRARY}
        z
        m
    )

    set_target_properties(bench_scenarios PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED ON
    )
endif()

# ============================================================================
# Benchmark 8: XML Serialization (8 tests)
# ============================================================================

if(PUGIXML_AVAILABLE)
    add_executable(bench_serialize
        bench_serialize.cpp
        ${BENCHMARK_COMMON}
    )

    target_include_directories(bench_serialize PRIVATE
        ${CMAKE_SOURCE_DIR}/src/include
        ${PUGIXML_INCLUDE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
    )

    target_link_libraries(bench_serialize PRIVATE
        taurus
        ${PUGIXML_LIBRARY}
    )

    set_target_properties(bench_serialize PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED ON
    )
endif()

# ============================================================================
# Master runner target
# ============================================================================

add_custom_target(run_all_benchmarks
    COMMAND ${CMAKE_COMMAND} -E echo "========================================"
    COMMAND ${CMAKE_COMMAND} -E echo "  Taurus Comprehensive Benchmark Suite  "
    COMMAND ${CMAKE_COMMAND} -E echo "========================================"
    COMMAND ${CMAKE_COMMAND} -E echo ""
    DEPENDS
        bench_parsing
        bench_traversal
        bench_attributes
        bench_modification
        bench_xpath_all
        bench_memory
        bench_scenarios
        bench_serialize
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running all comprehensive benchmarks"
)

message(STATUS "")
message(STATUS "Benchmark Suite configured:")
message(STATUS "  - bench_parsing: XML Parsing (10 tests)")
message(STATUS "  - bench_traversal: DOM Traversal (5 tests)")
message(STATUS "  - bench_attributes: Attribute Access (12 tests)")
message(STATUS "  - bench_modification: DOM Modification (8 tests)")
message(STATUS "  - bench_xpath_all: XPath All (20 tests)")
message(STATUS "  - bench_memory: Memory Efficiency (4 tests)")
message(STATUS "  - bench_scenarios: Real-World Scenarios (4 tests)")
message(STATUS "  - bench_serialize: XML Serialization (8 tests)")
message(STATUS "")
message(STATUS "Run with: cmake --build build --target run_all_benchmarks")
message(STATUS "Or run: ./benchmarks/suite/run_all_benchmarks.sh")
