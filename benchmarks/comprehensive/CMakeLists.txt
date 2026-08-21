# Comprehensive Benchmark Suite
# Measures 6 different scenarios to provide complete performance picture

cmake_minimum_required(VERSION 3.20)

# Find pugixml (standard CMake find_package)
find_package(pugixml QUIET)

if(NOT pugixml_FOUND)
    # Try standard find_path/find_library
    find_path(PUGIXML_INCLUDE_DIR
        NAMES pugixml.hpp
        PATH_SUFFIXES pugixml src
    )

    find_library(PUGIXML_LIBRARY
        NAMES pugixml
        PATH_SUFFIXES build lib
    )

    if(NOT PUGIXML_INCLUDE_DIR OR NOT PUGIXML_LIBRARY)
        message(WARNING "pugixml not found. Comprehensive benchmarks will not be built.")
        set(PUGIXML_AVAILABLE FALSE)
    else()
        set(PUGIXML_AVAILABLE TRUE)
    endif()
else()
    set(PUGIXML_AVAILABLE TRUE)
    set(PUGIXML_INCLUDE_DIR ${PUGIXML_INCLUDE_DIRS})
    set(PUGIXML_LIBRARY ${PUGIXML_LIBRARIES})
endif()

if(PUGIXML_AVAILABLE)
    message(STATUS "pugixml include: ${PUGIXML_INCLUDE_DIR}")
    message(STATUS "pugixml library: ${PUGIXML_LIBRARY}")

    # ============================================================================
    # Scenario 1: Parse Only (Cold Start)
    # ============================================================================
    add_executable(benchmark_parse
        benchmark_parse.cpp
    )

    target_include_directories(benchmark_parse PRIVATE
        ${CMAKE_SOURCE_DIR}/src/include
        ${PUGIXML_INCLUDE_DIR}
    )

    target_link_libraries(benchmark_parse PRIVATE
        leptris
        ${PUGIXML_LIBRARY}
    )

    set_target_properties(benchmark_parse PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED ON
    )

    add_custom_target(run_benchmark_parse
        COMMAND benchmark_parse
        DEPENDS benchmark_parse
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running Scenario 1: Parse Only Benchmark"
    )

    # ============================================================================
    # Scenario 2: First Access (Cold Operations)
    # ============================================================================
    add_executable(benchmark_first_access
        benchmark_first_access.cpp
    )

    target_include_directories(benchmark_first_access PRIVATE
        ${CMAKE_SOURCE_DIR}/src/include
        ${PUGIXML_INCLUDE_DIR}
    )

    target_link_libraries(benchmark_first_access PRIVATE
        leptris
        ${PUGIXML_LIBRARY}
    )

    set_target_properties(benchmark_first_access PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED ON
    )

    add_custom_target(run_benchmark_first_access
        COMMAND benchmark_first_access
        DEPENDS benchmark_first_access
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running Scenario 2: First Access Benchmark"
    )

    # ============================================================================
    # Scenario 4: Parse + Single Operations
    # ============================================================================
    add_executable(benchmark_parse_ops
        benchmark_parse_ops.cpp
    )

    target_include_directories(benchmark_parse_ops PRIVATE
        ${CMAKE_SOURCE_DIR}/src/include
        ${PUGIXML_INCLUDE_DIR}
    )

    target_link_libraries(benchmark_parse_ops PRIVATE
        leptris
        ${PUGIXML_LIBRARY}
    )

    set_target_properties(benchmark_parse_ops PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED ON
    )

    add_custom_target(run_benchmark_parse_ops
        COMMAND benchmark_parse_ops
        DEPENDS benchmark_parse_ops
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running Scenario 4: Parse + Operations Benchmark"
    )

    # ============================================================================
    # Scenario 6: Memory Usage
    # ============================================================================
    add_executable(benchmark_memory
        benchmark_memory.cpp
    )

    target_include_directories(benchmark_memory PRIVATE
        ${CMAKE_SOURCE_DIR}/src/include
        ${PUGIXML_INCLUDE_DIR}
    )

    target_link_libraries(benchmark_memory PRIVATE
        leptris
        ${PUGIXML_LIBRARY}
    )

    set_target_properties(benchmark_memory PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED ON
    )

    add_custom_target(run_benchmark_memory
        COMMAND benchmark_memory
        DEPENDS benchmark_memory
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running Scenario 6: Memory Usage Benchmark"
    )

    # ============================================================================
    # Master Benchmark Runner (runs all scenarios)
    # ============================================================================
    add_custom_target(run_comprehensive_benchmarks
        COMMAND ${CMAKE_COMMAND} -E echo "========================================"
        COMMAND ${CMAKE_COMMAND} -E echo "Comprehensive Benchmark Suite"
        COMMAND ${CMAKE_COMMAND} -E echo "========================================"
        COMMAND ${CMAKE_COMMAND} -E echo ""
        COMMAND ${CMAKE_COMMAND} -E echo "Scenario 1: Parse Only"
        COMMAND benchmark_parse
        COMMAND ${CMAKE_COMMAND} -E echo ""
        COMMAND ${CMAKE_COMMAND} -E echo "Scenario 2: First Access"
        COMMAND benchmark_first_access
        COMMAND ${CMAKE_COMMAND} -E echo ""
        COMMAND ${CMAKE_COMMAND} -E echo "Scenario 4: Parse + Operations"
        COMMAND benchmark_parse_ops
        COMMAND ${CMAKE_COMMAND} -E echo ""
        COMMAND ${CMAKE_COMMAND} -E echo "Scenario 6: Memory Usage"
        COMMAND benchmark_memory
        DEPENDS benchmark_parse benchmark_first_access benchmark_parse_ops benchmark_memory
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running all comprehensive benchmarks"
    )

    message(STATUS "Comprehensive benchmarks configured:")
    message(STATUS "  - benchmark_parse: Parse Only (Cold Start)")
    message(STATUS "  - benchmark_first_access: First Access (Cold Operations)")
    message(STATUS "  - benchmark_parse_ops: Parse + Single Operations")
    message(STATUS "  - benchmark_memory: Memory Usage")
    message(STATUS "")
    message(STATUS "Run with: make run_comprehensive_benchmarks")
endif()
