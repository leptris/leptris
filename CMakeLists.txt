# Taurus - Fast XML Parser and XPath Evaluator
# Pure C library with Ruby bindings and CLI
cmake_minimum_required(VERSION 3.20)

project(taurus
    VERSION 0.5.6
    DESCRIPTION "Fast XML parser and XPath evaluator in pure C"
    LANGUAGES C CXX
)

# C99 standard required
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# C++11 standard for benchmarks
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build options
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)  # Default to static
option(BUILD_TESTING "Build tests" ON)
option(BUILD_EXAMPLES "Build examples" OFF)
option(TAURUS_BUILD_CLI "Build CLI tool" ON)
option(TAURUS_BUILD_MAN_PAGES "Generate man pages with asciidoctor" OFF)
option(TAURUS_BUILD_BENCHMARKS "Build performance benchmarks" OFF)
option(TAURUS_ENABLE_UTF8PROC "Enable Unicode support via utf8proc" ON)
option(TAURUS_ENABLE_ICONV "Enable encoding conversion via iconv" ON)
option(TAURUS_ENABLE_ASAN "Enable AddressSanitizer (catches heap bugs at runtime)" OFF)
option(TAURUS_ENABLE_FUZZING "Build libFuzzer harness" OFF)
option(TAURUS_BUILD_DOCS "Build Doxygen API docs" OFF)

# LTO default: ON for Release/RelWithDebInfo, OFF for Debug/empty.
# The user can override with -DTAURUS_ENABLE_LTO=OFF.
# LTO delivers 1.3-3.5x speedups on parse/SAX/DOM benchmarks — see
# PR #74 for the benchmark data.  Downside: 5-30s longer link time.
# CheckIPOSupported guards against unsupported toolchains.
if(NOT DEFINED TAURUS_ENABLE_LTO)
    if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
        set(TAURUS_ENABLE_LTO_DEFAULT ON)
    else()
        set(TAURUS_ENABLE_LTO_DEFAULT OFF)
    endif()
else()
    set(TAURUS_ENABLE_LTO_DEFAULT ${TAURUS_ENABLE_LTO})
endif()
option(TAURUS_ENABLE_LTO "Enable link-time optimization (default ON for Release)" ${TAURUS_ENABLE_LTO_DEFAULT})

# AddressSanitizer — see TODO 35.
if(TAURUS_ENABLE_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address)
    message(STATUS "AddressSanitizer: ENABLED")
endif()

# Link-time optimization (TODO 110).
#
# Lets the compiler inline across translation-unit boundaries.  Without
# LTO, every "static inline" helper in element_modify.c / element_query.c
# that crosses a TU boundary from a caller in another .c file is a real
# function call — including taurus_pool_alloc, taurus_element_set_attribute,
# the chartype table lookups, etc.  With LTO, the compiler sees the whole
# program at link time and inlines aggressively.
#
# Off by default because:
#   - LTO significantly increases link time (5-30s for taurus)
#   - Some older toolchains have LTO bugs
#   - Debug builds don't benefit
#
# Recommended for release builds where every cycle counts.
if(TAURUS_ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT lto_supported OUTPUT lto_error)
    if(lto_supported)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
        message(STATUS "Link-time optimization: ENABLED")
    else()
        message(WARNING "TAURUS_ENABLE_LTO requested but not supported: ${lto_error}")
    endif()
endif()

# libFuzzer — see TODO 40.  Implies ASAN.
if(TAURUS_ENABLE_FUZZING)
    add_compile_options(-fsanitize=fuzzer-no-link,address -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=fuzzer,address)
    message(STATUS "libFuzzer: ENABLED (implies ASAN)")
endif()

# Doxygen API docs — see TODO 63.
if(TAURUS_BUILD_DOCS)
    find_package(Doxygen)
    if(DOXYGEN_FOUND)
        add_custom_target(docs
            COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_SOURCE_DIR}/docs/Doxyfile
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/docs
            COMMENT "Generating Doxygen API documentation")
        message(STATUS "Doxygen: ENABLED (target: docs)")
    else()
        message(WARNING "TAURUS_BUILD_DOCS=ON but Doxygen not found; skipping")
    endif()
endif()

# Position independent code for static libraries
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Hide all symbols by default; the public surface is exactly the
# functions marked TAURUS_API. Cuts the shared-library export table
# from ~445 entries down to the ~295 taurus_* symbols. See TODO 80.
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

# Configuration
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Add cmake modules path
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules")

# Include AdocMan for man page generation
if(TAURUS_BUILD_MAN_PAGES)
    include(AdocMan)
endif()

# Add subdirectories
add_subdirectory(src)

if(TAURUS_BUILD_CLI)
    add_subdirectory(cli)
endif()

if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(test)
endif()

if(BUILD_EXAMPLES)
    add_subdirectory(examples/c)
endif()

if(TAURUS_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

# Zero-copy test executable (disabled - not needed for benchmarks)
# add_executable(test_zero_copy test_zero_copy.c)
# target_link_libraries(test_zero_copy taurus)

# Install CMake config files
include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/taurus-config-version.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/taurus-config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/taurus-config.cmake"
    INSTALL_DESTINATION lib/cmake/taurus
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/taurus-config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/taurus-config-version.cmake"
    DESTINATION lib/cmake/taurus
)


# pkg-config file
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/taurus.pc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/taurus.pc"
    @ONLY
)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/taurus.pc"
    DESTINATION lib/pkgconfig
)

# Generate and install man pages (when building CLI)
if(TAURUS_BUILD_CLI AND TAURUS_BUILD_MAN_PAGES)
    # Use AdocMan.cmake to generate man pages from AsciiDoc sources
    # CLI man pages (section 1): cli/man/taurus.1.adoc, etc.
    set(ADOC_MAN_SOURCES
        cli/man/taurus.1.adoc
        cli/man/taurus-parse.1.adoc
        cli/man/taurus-xpath.1.adoc
        cli/man/taurus-format.1.adoc
    )

    foreach(ADOC_SOURCE ${ADOC_MAN_SOURCES})
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${ADOC_SOURCE}")
            add_adoc_man("${CMAKE_CURRENT_SOURCE_DIR}/${ADOC_SOURCE}")
        endif()
    endforeach()
endif()

# Generate library API man page (section 5)
if(TAURUS_BUILD_MAN_PAGES)
    # Library man page (section 5): src/man/libtaurus.5.adoc
    set(LIB_MAN_SOURCE src/man/libtaurus.5.adoc)

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${LIB_MAN_SOURCE}")
        add_adoc_man("${CMAKE_CURRENT_SOURCE_DIR}/${LIB_MAN_SOURCE}")
    endif()
endif()

# Summary
message(STATUS "")
message(STATUS "Taurus ${PROJECT_VERSION} Configuration:")
message(STATUS "  C Compiler: ${CMAKE_C_COMPILER}")
message(STATUS "  C Standard: C${CMAKE_C_STANDARD}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Shared Libraries: ${BUILD_SHARED_LIBS}")
message(STATUS "  Build Tests: ${BUILD_TESTING}")
message(STATUS "  Build Examples: ${BUILD_EXAMPLES}")
message(STATUS "  Build CLI: ${TAURUS_BUILD_CLI}")
message(STATUS "  Generate Man Pages: ${TAURUS_BUILD_MAN_PAGES}")
message(STATUS "  Install Prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "")
# Update the summary to include benchmarks
message(STATUS "  Build Benchmarks: ${TAURUS_BUILD_BENCHMARKS}")
