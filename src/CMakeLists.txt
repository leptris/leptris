# Leptris Library - Pure C XML parser and XPath evaluator

# ============================================================================
# Library Build Options
# ============================================================================

option(LEPTRIS_BUILD_STATIC "Build static library" ON)
option(LEPTRIS_BUILD_SHARED "Build shared library" OFF)
option(LEPTRIS_AMALGAMATED "Build the library from a single generated .c (TODO 170)" OFF)

# Ensure at least one library type is built
if(NOT LEPTRIS_BUILD_STATIC AND NOT LEPTRIS_BUILD_SHARED)
    message(FATAL_ERROR "At least one of LEPTRIS_BUILD_STATIC or LEPTRIS_BUILD_SHARED must be enabled")
endif()

# ============================================================================
# Source Files
# ============================================================================

set(LEPTRIS_SOURCES
    # Core: version + memory management (phase 2 extraction)
    leptris/core.c
    # DOM nodes
    leptris/dom/node.c
    leptris/dom/node_vtable.c
    leptris/dom/node_public.c
    leptris/dom/element.c
    leptris/dom/element_query.c
    leptris/dom/element_modify.c
    leptris/dom/element_index.c
    leptris/dom/text.c
    leptris/dom/comment.c
    leptris/dom/cdata.c
    leptris/dom/pi.c
    leptris/dom/doctype.c

    # Memory management
    leptris/memory/pool.c
    leptris/memory/arena.c
    leptris/memory/compact_allocator.c
    leptris/dom/compact.c
    leptris/dom/root_doc_map.c
    leptris/leptris_memory.c

    # Common utilities
    leptris/common/string_view.c
    leptris/common/entities.c
    leptris/common/chartype.c
    # AOT SIMD framework (TODO 175): scalar impls + runtime dispatch
    leptris/common/cpu.c
    leptris/common/simd_text.c
    # ISA-specific TUs added below (they need per-file compile flags)

    # Encoding (UTF-16 support - always available)
    leptris/encoding/utf16.c
    leptris/encoding/unicode.c
    leptris/encoding/wrapper.c

    # Parser and Serializer
    leptris/serialize/serialize.c
    leptris/serialize/c14n.c

    # XPath
    leptris/xpath/lexer.c
    leptris/xpath/parser.c
    leptris/xpath/compiler.c
    leptris/xpath/vm.c
    leptris/xpath/evaluator.c
    leptris/xpath/evaluator_types.c
    leptris/xpath/evaluator_axes.c
    leptris/xpath/evaluator_operators.c
    leptris/xpath/evaluator_path.c
    leptris/xpath/functions.c
    leptris/xpath/functions_exslt.c
    leptris/xpath/xpath_variables.c
    leptris/xpath/xpath_public.c
    leptris/xpath/xpath_compiled.c
    leptris/xpath/xpath_ast_cache.c

    # SAX
    leptris/sax/parser.c
    leptris/sax/pull.c
    leptris/sax/iterparse.c
    leptris/sax/streaming.c

    # DTD
    leptris/dtd/model.c
    leptris/dtd/parser.c
    leptris/dtd/resolver.c
    leptris/dtd/validator.c
    leptris/dtd/content_check.c

    # XInclude
    leptris/xinclude/xinclude.c

    # The sole XML parser: direct_parse (single-pass, zero-copy,
    # bulk-alloc, DTD-aware, UTF-8 names). See flat/direct_parse.c.
    # The FlatDoc + lazy-promote architecture (TODO 139) and the
    # legacy parser (TODO 90 Phase 1) have been removed.
    leptris/flat/direct_parse.c

    # Core library
    leptris/leptris.c
    leptris/error.c
)

# ============================================================================
# Amalgamation build (TODO 170)
# ============================================================================
# Generate a single .c file that #includes all internal sources, then build
# the library from this one TU instead of the per-file object library. Gets
# cross-TU inlining without requiring LTO; useful for toolchains that lack
# reliable LTO, or for distribution as a single source file.
#
# This is the pugixml approach (one 13,800-line .cpp). When LEPTRIS_AMALGAMATED
# is ON, the object library wraps the single generated file instead of the
# per-file source list.
if(LEPTRIS_AMALGAMATED)
    set(LEPTRIS_AMALGAMATED_C "${CMAKE_CURRENT_BINARY_DIR}/leptris_amalgamated.c")
    file(WRITE "${LEPTRIS_AMALGAMATED_C}"
"/* Auto-generated amalgamation build (TODO 170). Do not edit.\n"
" * Generated from ${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt at configure time.\n"
" * Build with -DLEPTRIS_AMALGAMATED=ON. The whole library compiles as one\n"
" * translation unit so static helpers inline across what would otherwise be\n"
" * TU boundaries — the same effect as LTO but at compile time.\n"
" */\n\n")

    # Append #include for every source. Paths are relative to src/ so the
    # file's own #include directives resolve identically to the per-file build.
    foreach(_src ${LEPTRIS_SOURCES})
        file(APPEND "${LEPTRIS_AMALGAMATED_C}" "#include \"${_src}\"\n")
    endforeach()

    # Replace the source list with the amalgamated file so the object
    # library wraps a single TU.
    set(LEPTRIS_SOURCES_AMALGAMATED "${LEPTRIS_AMALGAMATED_C}")
    message(STATUS "Amalgamation build: ENABLED → ${LEPTRIS_AMALGAMATED_C}")
else()
    set(LEPTRIS_SOURCES_AMALGAMATED "${LEPTRIS_SOURCES}")
endif()

# ============================================================================
# Object Library (shared between static and shared)
# ============================================================================

if(LEPTRIS_AMALGAMATED)
    add_library(leptris_objects OBJECT ${LEPTRIS_SOURCES_AMALGAMATED})

    # The amalgamated TU needs the include search paths from src/ downward.
    target_include_directories(leptris_objects PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})
else()
    add_library(leptris_objects OBJECT ${LEPTRIS_SOURCES})
endif()

# Version defines reach core.c through the object library in BOTH
# build modes — without these the stale hand-written fallback in
# core.c leaks into leptris_version()/leptris_version_components().
target_compile_definitions(leptris_objects PRIVATE
    LEPTRIS_VERSION="${PROJECT_VERSION}"
    LEPTRIS_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    LEPTRIS_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    LEPTRIS_VERSION_PATCH=${PROJECT_VERSION_PATCH})

set_target_properties(leptris_objects PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON  # Important for shared libraries
    # TODO.concurrency/02: internal symbols stay resolvable inside the
    # final binary (tests/CLI embed these objects) but never export
    # from the shared library; only LEPTRIS_API (visibility default)
    # exports. Windows is already narrow via dllexport-only.
    C_VISIBILITY_PRESET hidden
)

target_include_directories(leptris_objects
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/leptris/dom
        ${CMAKE_CURRENT_SOURCE_DIR}/leptris/parse
        ${CMAKE_CURRENT_SOURCE_DIR}/leptris/serialize
        ${CMAKE_CURRENT_SOURCE_DIR}/leptris/xpath
        ${CMAKE_CURRENT_SOURCE_DIR}/leptris/encoding
        ${CMAKE_CURRENT_SOURCE_DIR}/leptris/memory
        ${CMAKE_CURRENT_SOURCE_DIR}/leptris/flat
)

# Math library: libm is separate on Linux/BSD, folded into libc on
# macOS, and folded into the regular CRT on Windows. Set a list that
# downstream targets link against; on Windows it's empty.
if(WIN32)
    set(LEPTRIS_MATH_LIBS "")
elseif(APPLE)
    set(LEPTRIS_MATH_LIBS "")
else()
    set(LEPTRIS_MATH_LIBS m)
endif()

# Link libm only on platforms where it's a separate library.
if(LEPTRIS_MATH_LIBS)
    target_link_libraries(leptris_objects PUBLIC ${LEPTRIS_MATH_LIBS})
endif()

# Define _POSIX_C_SOURCE so strdup() and other POSIX functions are
# properly declared by <string.h>. Without this, strict C99 mode
# implicitly declares strdup as `int strdup(...)` returning 4 bytes,
# which truncates 64-bit pointers on Linux x86_64. macOS happens to
# work because its libc returns pointers that fit in 32 bits.
# See TODOs 94 / 95 for the bug this caused.
target_compile_definitions(leptris_objects PUBLIC _POSIX_C_SOURCE=200809L)

# ============================================================================
# AOT SIMD sources (TODO 175)
# ============================================================================
# The simdjson model: compile ISA-specific TUs with their own flags, link
# them all into the same library, dispatch at runtime via cpu.c. The
# scalar TU (simd_text.c) is always built; these add accelerated paths.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "(x86_64|AMD64|i[3-6]86)")
    check_c_compiler_flag("-mavx2" LEPTRIS_HAVE_AVX2_FLAG)
    if(LEPTRIS_HAVE_AVX2_FLAG)
        target_sources(leptris_objects PRIVATE leptris/common/simd_text_avx2.c)
        set_source_files_properties(leptris/common/simd_text_avx2.c
            PROPERTIES COMPILE_FLAGS "-mavx2")
        # Consumers (simd_text.c dispatch) reference the AVX2 symbol only
        # when this define is present — MSVC rejects -mavx2 (uses /arch:),
        # so the TU is skipped there and the dispatch falls back to scalar.
        target_compile_definitions(leptris_objects PRIVATE LEPTRIS_HAS_AVX2_BUILD=1)
        message(STATUS "AOT SIMD: AVX2 text scan ENABLED")
    else()
        message(STATUS "AOT SIMD: AVX2 unavailable (flag rejected)")
    endif()
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "(aarch64|arm64|ARM64)")
    # NEON is baseline on aarch64 — no special flags needed.
    target_sources(leptris_objects PRIVATE leptris/common/simd_text_neon.c)
    message(STATUS "AOT SIMD: NEON text scan ENABLED")
endif()

target_compile_options(leptris_objects PRIVATE
    $<$<OR:$<C_COMPILER_ID:GNU>,$<C_COMPILER_ID:Clang>,$<C_COMPILER_ID:AppleClang>>:
        -Wall -Wextra -Wno-unused-parameter>
    $<$<C_COMPILER_ID:MSVC>:
        /W4 /wd4100 /wd4201 /wd4204 /wd4244 /wd4267 /wd4210 /wd4996>
    $<$<AND:$<OR:$<C_COMPILER_ID:GNU>,$<C_COMPILER_ID:Clang>,$<C_COMPILER_ID:AppleClang>>,$<CONFIG:Release>>:-O3>
    $<$<AND:$<OR:$<C_COMPILER_ID:GNU>,$<C_COMPILER_ID:Clang>,$<C_COMPILER_ID:AppleClang>>,$<CONFIG:Debug>>:-g -O0>
    $<$<AND:$<C_COMPILER_ID:MSVC>,$<CONFIG:Release>>:/O2>
    $<$<AND:$<C_COMPILER_ID:MSVC>,$<CONFIG:Debug>>:/Od /Zi>
)

# MSVC's CRT deprecates strdup/strncpy/sprintf as "unsafe" (C4996).
# Project-wide defines silence the noise — these functions are used
# safely throughout, and the *_s variants have different signatures
# that would be a much bigger portability headache.
if(MSVC)
    target_compile_definitions(leptris_objects PRIVATE
        _CRT_NONSTDC_NO_DEPRECATE
        _CRT_SECURE_NO_WARNINGS
        _CRT_SECURE_NO_DEPRECATE
    )
endif()

# ============================================================================
# Optional Dependencies
# ============================================================================

# utf8proc for Unicode support
if(LEPTRIS_ENABLE_UTF8PROC)
    # Three lookup strategies, in order of preference:
    #   1. Native utf8proc CONFIG package (Homebrew, system packages, from-source).
    #   2. vcpkg's unofficial-utf8proc CONFIG package.
    #   3. Module-mode finder shipped in cmake/FindUtf8proc.cmake.
    find_package(utf8proc CONFIG QUIET)
    if(utf8proc_FOUND)
        set(LEPTRIS_UTF8PROC_TARGET utf8proc::utf8proc)
    else()
        find_package(unofficial-utf8proc CONFIG QUIET)
        if(unofficial-utf8proc_FOUND)
            set(LEPTRIS_UTF8PROC_TARGET unofficial::utf8proc)
        else()
            find_package(Utf8proc REQUIRED)
            set(LEPTRIS_UTF8PROC_TARGET Utf8proc::Utf8proc)
        endif()
    endif()

    # PUBLIC: the link dependency must propagate through $<TARGET_OBJECTS:...>
    # aggregation into leptris_static / leptris_shared and onward to consumers.
    target_link_libraries(leptris_objects PUBLIC ${LEPTRIS_UTF8PROC_TARGET})
    target_compile_definitions(leptris_objects PUBLIC LEPTRIS_HAS_UTF8PROC=1)

    target_sources(leptris_objects PRIVATE
        leptris/unicode/unicode.c
    )

    target_include_directories(leptris_objects PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/leptris/unicode
    )

    message(STATUS "utf8proc: ENABLED via ${LEPTRIS_UTF8PROC_TARGET}")
else()
    message(STATUS "utf8proc: DISABLED")
endif()

# iconv for encoding conversion
if(LEPTRIS_ENABLE_ICONV)
    find_package(Iconv REQUIRED)

    # PUBLIC: see utf8proc note above.
    target_link_libraries(leptris_objects PUBLIC Iconv::Iconv)
    target_compile_definitions(leptris_objects PUBLIC LEPTRIS_HAS_ICONV=1)

    target_sources(leptris_objects PRIVATE
        leptris/encoding/encoding.c
    )

    target_include_directories(leptris_objects PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/leptris/encoding
    )

    message(STATUS "iconv: ENABLED")
else()
    message(STATUS "iconv: DISABLED")
endif()

message(STATUS "Compact mode: ENABLED (96-byte elements vs 192-byte legacy = 2x reduction!)")

# ============================================================================
# Static Library
# ============================================================================

if(LEPTRIS_BUILD_STATIC)
    add_library(leptris_static STATIC $<TARGET_OBJECTS:leptris_objects>)

    set_target_properties(leptris_static PROPERTIES
        OUTPUT_NAME leptris
        C_STANDARD 99
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )

    # Include directories (same as object library)
    target_include_directories(leptris_static
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/leptris/dom
            ${CMAKE_CURRENT_SOURCE_DIR}/leptris/parse
            ${CMAKE_CURRENT_SOURCE_DIR}/leptris/serialize
            ${CMAKE_CURRENT_SOURCE_DIR}/leptris/xpath
            ${CMAKE_CURRENT_SOURCE_DIR}/leptris/encoding
            ${CMAKE_CURRENT_SOURCE_DIR}/leptris/memory
            ${CMAKE_CURRENT_SOURCE_DIR}/leptris/flat
    )

    # Pull in link dependencies (utf8proc, Iconv, m) declared on
    # leptris_objects.  CMake's $<TARGET_OBJECTS:...> aggregation does NOT
    # propagate OBJECT-library link deps to the static lib's consumers
    # (CMake issue #16093), so we re-state them here as PUBLIC to ensure
    # `leptris::leptris` consumers link iconv/utf8proc/m automatically.
    if(LEPTRIS_MATH_LIBS)
        target_link_libraries(leptris_static PUBLIC ${LEPTRIS_MATH_LIBS})
    endif()
    if(LEPTRIS_ENABLE_UTF8PROC)
        target_link_libraries(leptris_static PUBLIC ${LEPTRIS_UTF8PROC_TARGET})
    endif()
    if(LEPTRIS_ENABLE_ICONV)
        target_link_libraries(leptris_static PUBLIC Iconv::Iconv)
    endif()

    add_library(leptris::static ALIAS leptris_static)
    message(STATUS "Static library: ENABLED")
endif()

# ============================================================================
# Shared Library
# ============================================================================

if(LEPTRIS_BUILD_SHARED)
    add_library(leptris_shared SHARED $<TARGET_OBJECTS:leptris_objects>)

    # Set version properties for shared library
    # This creates: libleptris.so.0.3.0 -> libleptris.so.0 -> libleptris.so
    # TODO.concurrency/05: name the DLL libleptris.dll on Windows so
    # vendoring tooling needs no per-platform special case (every
    # other platform already gets the lib prefix).
    if(WIN32)
        set(LEPTRIS_SHARED_OUTPUT_NAME libleptris)
    else()
        set(LEPTRIS_SHARED_OUTPUT_NAME leptris)
    endif()
    set_target_properties(leptris_shared PROPERTIES
        VERSION ${PROJECT_VERSION}        # e.g., 0.3.0
        SOVERSION ${PROJECT_VERSION_MAJOR}  # e.g., 0
        OUTPUT_NAME ${LEPTRIS_SHARED_OUTPUT_NAME}
        C_STANDARD 99
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )

    # Export symbols for Windows DLL. The definition must reach the
    # compilation of the .c files, which happens in leptris_objects —
    # a PRIVATE definition here never propagates to an object
    # library's sources, so the DLL exported nothing and MSVC
    # emitted no import library (issue #278).
    target_compile_definitions(leptris_shared PRIVATE LEPTRIS_BUILDING_DLL)
    if(WIN32)
        target_compile_definitions(leptris_objects PRIVATE LEPTRIS_BUILDING_DLL)
    endif()
    if(WIN32)
        target_compile_definitions(leptris_shared PUBLIC LEPTRIS_DLL)
    endif()

    # Versioned symbol visibility (TODO.remaining/10): ELF toolchains
    # get a version script binding the public surface to the LEPTRIS_1
    # version node, everything else local. macOS (no --version-script)
    # and Windows (dllexport-based) skip it.
    if(NOT APPLE AND NOT WIN32 AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_link_options(leptris_shared PRIVATE
            "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/leptris.symvers")
    endif()

    # Include directories (same as object library)
    target_include_directories(leptris_shared
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/leptris/dom
            ${CMAKE_CURRENT_SOURCE_DIR}/leptris/parse
            ${CMAKE_CURRENT_SOURCE_DIR}/leptris/serialize
            ${CMAKE_CURRENT_SOURCE_DIR}/leptris/xpath
            ${CMAKE_CURRENT_SOURCE_DIR}/leptris/encoding
            ${CMAKE_CURRENT_SOURCE_DIR}/leptris/memory
            ${CMAKE_CURRENT_SOURCE_DIR}/leptris/flat
    )

    # See note on leptris_static above.
    if(LEPTRIS_MATH_LIBS)
        target_link_libraries(leptris_shared PUBLIC ${LEPTRIS_MATH_LIBS})
    endif()
    if(LEPTRIS_ENABLE_UTF8PROC)
        target_link_libraries(leptris_shared PUBLIC ${LEPTRIS_UTF8PROC_TARGET})
    endif()
    if(LEPTRIS_ENABLE_ICONV)
        target_link_libraries(leptris_shared PUBLIC Iconv::Iconv)
    endif()

    add_library(leptris::shared ALIAS leptris_shared)
    message(STATUS "Shared library: ENABLED")
endif()

# ============================================================================
# Main "leptris" Target (Compatibility Alias)
# ============================================================================

# The main leptris target points to the appropriate library
if(LEPTRIS_BUILD_SHARED AND NOT LEPTRIS_BUILD_STATIC)
    # Shared only
    add_library(leptris ALIAS leptris_shared)
    add_library(leptris::leptris ALIAS leptris_shared)
elseif(LEPTRIS_BUILD_STATIC AND NOT LEPTRIS_BUILD_SHARED)
    # Static only
    add_library(leptris ALIAS leptris_static)
    add_library(leptris::leptris ALIAS leptris_static)
else()
    # Both built - default to shared for linking
    add_library(leptris ALIAS leptris_shared)
    add_library(leptris::leptris ALIAS leptris_shared)
endif()

# ============================================================================
# Installation
# ============================================================================

include(GNUInstallDirs)

# Install static library (Development component)
if(LEPTRIS_BUILD_STATIC)
    install(TARGETS leptris_static
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
                COMPONENT leptris_Development
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
                COMPONENT leptris_Development
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
                COMPONENT leptris_Development
    )
endif()

# Install shared library (separate Runtime and Development components)
if(LEPTRIS_BUILD_SHARED)
    install(TARGETS leptris_shared
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
                COMPONENT leptris_Development
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
                COMPONENT leptris_Runtime
                NAMELINK_COMPONENT leptris_Development
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
                COMPONENT leptris_Runtime
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
                COMPONENT leptris_Development
    )
endif()

# Install public headers (Development component)
install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    COMPONENT leptris_Development
    FILES_MATCHING PATTERN "*.h"
)

# Build Requires line for optional dependencies (pkg-config)
set(LEPTRIS_PKG_CONFIG_REQUIRES "")
if(LEPTRIS_ENABLE_UTF8PROC)
    set(LEPTRIS_PKG_CONFIG_REQUIRES "${LEPTRIS_PKG_CONFIG_REQUIRES} utf8proc")
endif()
if(LEPTRIS_ENABLE_ICONV)
    if(LEPTRIS_PKG_CONFIG_REQUIRES)
        set(LEPTRIS_PKG_CONFIG_REQUIRES "${LEPTRIS_PKG_CONFIG_REQUIRES}, Iconv")
    else()
        set(LEPTRIS_PKG_CONFIG_REQUIRES "Iconv")
    endif()
endif()
# pkg-config file (Development component)
configure_file(
    ${CMAKE_SOURCE_DIR}/cmake/leptris.pc.in
    ${CMAKE_CURRENT_BINARY_DIR}/leptris.pc
    @ONLY
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/leptris.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
    COMPONENT leptris_Development
)

# ============================================================================
# Export Targets for CMake find_package()
# ============================================================================

include(CMakePackageConfigHelpers)

# Generate the config file for find_package()
configure_package_config_file(
    ${CMAKE_SOURCE_DIR}/cmake/leptris-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/leptris-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/leptris

)

# Generate the version file
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/leptris-config-version.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

# Install CMake config files
install(
    FILES
        ${CMAKE_CURRENT_BINARY_DIR}/leptris-config.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/leptris-config-version.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/leptris
    COMPONENT leptris_Development
)
