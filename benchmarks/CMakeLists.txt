# Taurus vs pugixml Benchmark Suite

cmake_minimum_required(VERSION 3.20)

# ============================================================================
# Find pugixml - Check multiple locations
# ============================================================================
# First, try external path (for local development)
set(PUGIXML_EXTERNAL_PATH "/Users/mulgogi/src/external/pugixml")

if(EXISTS "${PUGIXML_EXTERNAL_PATH}/CMakeLists.txt")
    message(STATUS "Found pugixml at external path: ${PUGIXML_EXTERNAL_PATH}")
    set(PUGIXML_EXTERNAL TRUE)
else()
    set(PUGIXML_EXTERNAL FALSE)
endif()

# Find pugixml (standard CMake find_package)
find_package(pugixml QUIET)

if(NOT pugixml_FOUND AND NOT PUGIXML_EXTERNAL)
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
        message(WARNING "pugixml not found. Benchmark targets will not be built.")
        set(PUGIXML_AVAILABLE FALSE)
    else()
        set(PUGIXML_AVAILABLE TRUE)
        set(PUGIXML_INCLUDE_DIR ${PUGIXML_INCLUDE_DIR})
        set(PUGIXML_LIBRARY ${PUGIXML_LIBRARY})
    endif()
elseif(pugixml_FOUND)
    set(PUGIXML_AVAILABLE TRUE)
    # CMake package may not set include dirs - try to find them
    if(NOT PUGIXML_INCLUDE_DIRS)
        find_path(_pugixml_inc NAMES pugixml.hpp PATH_SUFFIXES pugixml src)
        set(PUGIXML_INCLUDE_DIR ${_pugixml_inc})
        set(PUGIXML_INCLUDE_DIRS ${_pugixml_inc})
    else()
        set(PUGIXML_INCLUDE_DIR ${PUGIXML_INCLUDE_DIRS})
    endif()
    if(NOT PUGIXML_LIBRARIES)
        find_library(_pugixml_lib NAMES pugixml)
        set(PUGIXML_LIBRARY ${_pugixml_lib})
        set(PUGIXML_LIBRARIES ${_pugixml_lib})
    else()
        set(PUGIXML_LIBRARY ${PUGIXML_LIBRARIES})
    endif()
else()
    # Use external path - check if build exists
    set(PUGIXML_INCLUDE_DIR "${PUGIXML_EXTERNAL_PATH}/src")
    set(PUGIXML_LIBRARY "${PUGIXML_EXTERNAL_PATH}/build/libpugixml.a")

    if(EXISTS "${PUGIXML_LIBRARY}")
        set(PUGIXML_AVAILABLE TRUE)
    else()
        # Try to build pugixml from external source
        message(STATUS "Building pugixml from external source...")
        execute_process(
            COMMAND cmake -S "${PUGIXML_EXTERNAL_PATH}" -B "${PUGIXML_EXTERNAL_PATH}/build"
            COMMAND cmake --build "${PUGIXML_EXTERNAL_PATH}/build"
            WORKING_DIRECTORY "${PUGIXML_EXTERNAL_PATH}"
            RESULT_VARIABLE PUGIXML_BUILD )

        if(PUGIXML_BUILD EQUAL 0 AND EXISTS "${PUGIXML_LIBRARY}")
            message(STATUS "Successfully built pugixml from external source")
            set(PUGIXML_AVAILABLE TRUE)
        else()
            message(WARNING "Failed to build pugixml from external source")
            set(PUGIXML_AVAILABLE FALSE)
        endif()
    endif()
endif()

# Add unified vs duplication benchmark
add_executable(bench_unified_vs_duplication
    bench_unified_vs_duplication.c
    utils.c
)

target_include_directories(bench_unified_vs_duplication PRIVATE
    ${CMAKE_SOURCE_DIR}/src/include
)

target_link_libraries(bench_unified_vs_duplication PRIVATE
    taurus
)

set_target_properties(bench_unified_vs_duplication PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED on
)

add_custom_target(run_bench_unified_vs_duplication
    COMMAND bench_unified_vs_duplication
    DEPENDS bench_unified_vs_duplication
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Compare unified (compact+wrappers) vs duplicated (direct pointers) architecture"
)

if(PUGIXML_AVAILABLE)
    message(STATUS "pugixml include: ${PUGIXML_INCLUDE_DIR}")
    message(STATUS "pugixml library: ${PUGIXML_LIBRARY}")

    # DOM Modification Benchmark
    add_executable(bench_dom_pugixml
        workflows/bench_dom_pugixml.cpp
        pugixml_compat/dom_modify_tests.c
    )

    target_include_directories(bench_dom_pugixml PRIVATE
        ${CMAKE_SOURCE_DIR}/src/include
        ${PUGIXML_INCLUDE_DIR}
        pugixml_compat
    )

    target_link_libraries(bench_dom_pugixml PRIVATE
        taurus
        ${PUGIXML_LIBRARY}
    )

    # Enable C++11 for pugixml
    set_target_properties(bench_dom_pugixml PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED ON
    )

    # DOM Benchmark (parse once, measure operations in isolation)
    add_executable(dom_benchmark
        dom/dom_benchmark.cpp
        utils.c
    )

    target_include_directories(dom_benchmark PRIVATE
        ${CMAKE_SOURCE_DIR}/src/include
        ${PUGIXML_INCLUDE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_link_libraries(dom_benchmark PRIVATE
        taurus
        ${PUGIXML_LIBRARY}
    )

    set_target_properties(dom_benchmark PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED ON
    )

    # Comprehensive Benchmark (complete lifecycle analysis)
    add_executable(comprehensive_benchmark
        comprehensive_benchmark.cpp
        utils.c
    )

    target_include_directories(comprehensive_benchmark PRIVATE
        ${CMAKE_SOURCE_DIR}/src/include
        ${PUGIXML_INCLUDE_DIR}
    )

    target_link_libraries(comprehensive_benchmark PRIVATE
        taurus
        ${PUGIXML_LIBRARY}
    )

    set_target_properties(comprehensive_benchmark PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED ON
    )

    # Add test target
    add_custom_target(run_bench_dom_pugixml
        COMMAND bench_dom_pugixml
        DEPENDS bench_dom_pugixml
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running Taurus vs pugixml DOM modification benchmark"
    )

    # Parsing Benchmark (measure parse time only)
    add_executable(bench_dom_parse
        src/bench_dom_parse.cc
        utils.c
    )

    target_include_directories(bench_dom_parse PRIVATE
        ${CMAKE_SOURCE_DIR}/src/include
        ${PUGIXML_INCLUDE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_link_libraries(bench_dom_parse PRIVATE
        taurus
        ${PUGIXML_LIBRARY}
    )

    set_target_properties(bench_dom_parse PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED ON
    )

    # ============================================================================
    # Ultimate Benchmark - Taurus vs pugixml vs libxml2
    # ============================================================================
    find_package(LibXml2 QUIET)

    if(LibXml2_FOUND)
        message(STATUS "libxml2 found: ${LIBXML2_INCLUDE_DIR}")

        add_executable(ultimate_benchmark
            workflows/bench_ultimate.cpp
            utils.c
        )

        target_include_directories(ultimate_benchmark PRIVATE
            ${CMAKE_SOURCE_DIR}/src/include
            ${PUGIXML_INCLUDE_DIR}
            ${LIBXML2_INCLUDE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}
        )

        target_link_libraries(ultimate_benchmark PRIVATE
            taurus
            ${PUGIXML_LIBRARY}
            ${LIBXML2_LIBRARIES}
        )

        target_compile_definitions(ultimate_benchmark PRIVATE
            ${LIBXML2_DEFINITIONS}
        )

        set_target_properties(ultimate_benchmark PROPERTIES
            CXX_STANDARD 17
            CXX_STANDARD_REQUIRED ON
        )

        message(STATUS "Ultimate benchmark (Taurus vs pugixml vs libxml2) configured")
    else()
        message(WARNING "libxml2 not found. Ultimate benchmark will not be built.")
    endif()

endif()
