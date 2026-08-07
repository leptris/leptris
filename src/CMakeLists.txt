# Taurus Library - Pure C XML parser and XPath evaluator

# ============================================================================
# Library Build Options
# ============================================================================

option(TAURUS_BUILD_STATIC "Build static library" ON)
option(TAURUS_BUILD_SHARED "Build shared library" OFF)

# Ensure at least one library type is built
if(NOT TAURUS_BUILD_STATIC AND NOT TAURUS_BUILD_SHARED)
    message(FATAL_ERROR "At least one of TAURUS_BUILD_STATIC or TAURUS_BUILD_SHARED must be enabled")
endif()

# ============================================================================
# Source Files
# ============================================================================

set(TAURUS_SOURCES
    # Core: version + memory management (phase 2 extraction)
    taurus/core.c
    # DOM nodes
    taurus/dom/node.c
    taurus/dom/node_vtable.c
    taurus/dom/node_public.c
    taurus/dom/element.c
    taurus/dom/element_query.c
    taurus/dom/element_modify.c
    taurus/dom/element_index.c
    taurus/dom/text.c
    taurus/dom/comment.c
    taurus/dom/cdata.c
    taurus/dom/pi.c
    taurus/dom/doctype.c

    # Memory management
    taurus/memory/pool.c
    taurus/memory/compact_allocator.c
    taurus/dom/compact.c
    taurus/taurus_memory.c

    # Common utilities
    taurus/common/string_view.c
    taurus/common/entities.c

    # Encoding (UTF-16 support - always available)
    taurus/encoding/utf16.c
    taurus/encoding/unicode.c
    taurus/encoding/wrapper.c

    # Parser and Serializer
    taurus/parse/parser_new.c
    taurus/parse/compact_parser.c
    taurus/serialize/serialize.c
    taurus/serialize/c14n.c

    # XPath
    taurus/xpath/lexer.c
    taurus/xpath/parser.c
    taurus/xpath/compiler.c
    taurus/xpath/vm.c
    taurus/xpath/evaluator.c
    taurus/xpath/evaluator_types.c
    taurus/xpath/evaluator_axes.c
    taurus/xpath/evaluator_operators.c
    taurus/xpath/evaluator_path.c
    taurus/xpath/functions.c
    taurus/xpath/xpath_variables.c
    taurus/xpath/xpath_public.c
    taurus/xpath/xpath_ast_cache.c

    # SAX
    taurus/sax/parser.c
    taurus/sax/streaming.c

    # DTD
    taurus/dtd/model.c
    taurus/dtd/parser.c
    taurus/dtd/resolver.c
    taurus/dtd/validator.c
    taurus/dtd/content_check.c

    # XInclude
    taurus/xinclude/xinclude.c

    # Flat document buffer (TODO 139) — fast parse representation
    # that converts to the compact-pointer tree lazily on first access.
    taurus/flat/flat_doc.c
    taurus/flat/flat_parser.c

    # Core library
    taurus/taurus.c
    taurus/error.c
)

# ============================================================================
# Object Library (shared between static and shared)
# ============================================================================

add_library(taurus_objects OBJECT ${TAURUS_SOURCES})

set_target_properties(taurus_objects PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON  # Important for shared libraries
)

target_include_directories(taurus_objects
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/taurus/dom
        ${CMAKE_CURRENT_SOURCE_DIR}/taurus/parse
        ${CMAKE_CURRENT_SOURCE_DIR}/taurus/serialize
        ${CMAKE_CURRENT_SOURCE_DIR}/taurus/xpath
        ${CMAKE_CURRENT_SOURCE_DIR}/taurus/encoding
        ${CMAKE_CURRENT_SOURCE_DIR}/taurus/memory
        ${CMAKE_CURRENT_SOURCE_DIR}/taurus/flat
)

target_link_libraries(taurus_objects PUBLIC m)

# Define _POSIX_C_SOURCE so strdup() and other POSIX functions are
# properly declared by <string.h>. Without this, strict C99 mode
# implicitly declares strdup as `int strdup(...)` returning 4 bytes,
# which truncates 64-bit pointers on Linux x86_64. macOS happens to
# work because its libc returns pointers that fit in 32 bits.
# See TODOs 94 / 95 for the bug this caused.
target_compile_definitions(taurus_objects PUBLIC _POSIX_C_SOURCE=200809L)

target_compile_options(taurus_objects PRIVATE
    -Wall
    -Wextra
    -Wno-unused-parameter
    $<$<CONFIG:Release>:-O3>
    $<$<CONFIG:Debug>:-g -O0>
)

# ============================================================================
# Optional Dependencies
# ============================================================================

# utf8proc for Unicode support
if(TAURUS_ENABLE_UTF8PROC)
    # Three lookup strategies, in order of preference:
    #   1. Native utf8proc CONFIG package (Homebrew, system packages, from-source).
    #   2. vcpkg's unofficial-utf8proc CONFIG package.
    #   3. Module-mode finder shipped in cmake/FindUtf8proc.cmake.
    find_package(utf8proc CONFIG QUIET)
    if(utf8proc_FOUND)
        set(TAURUS_UTF8PROC_TARGET utf8proc::utf8proc)
    else()
        find_package(unofficial-utf8proc CONFIG QUIET)
        if(unofficial-utf8proc_FOUND)
            set(TAURUS_UTF8PROC_TARGET unofficial::utf8proc)
        else()
            find_package(Utf8proc REQUIRED)
            set(TAURUS_UTF8PROC_TARGET Utf8proc::Utf8proc)
        endif()
    endif()

    # PUBLIC: the link dependency must propagate through $<TARGET_OBJECTS:...>
    # aggregation into taurus_static / taurus_shared and onward to consumers.
    target_link_libraries(taurus_objects PUBLIC ${TAURUS_UTF8PROC_TARGET})
    target_compile_definitions(taurus_objects PUBLIC TAURUS_HAS_UTF8PROC=1)

    target_sources(taurus_objects PRIVATE
        taurus/unicode/unicode.c
    )

    target_include_directories(taurus_objects PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/taurus/unicode
    )

    message(STATUS "utf8proc: ENABLED via ${TAURUS_UTF8PROC_TARGET}")
else()
    message(STATUS "utf8proc: DISABLED")
endif()

# iconv for encoding conversion
if(TAURUS_ENABLE_ICONV)
    find_package(Iconv REQUIRED)

    # PUBLIC: see utf8proc note above.
    target_link_libraries(taurus_objects PUBLIC Iconv::Iconv)
    target_compile_definitions(taurus_objects PUBLIC TAURUS_HAS_ICONV=1)

    target_sources(taurus_objects PRIVATE
        taurus/encoding/encoding.c
    )

    target_include_directories(taurus_objects PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/taurus/encoding
    )

    message(STATUS "iconv: ENABLED")
else()
    message(STATUS "iconv: DISABLED")
endif()

message(STATUS "Compact mode: ENABLED (96-byte elements vs 192-byte legacy = 2x reduction!)")

# ============================================================================
# Static Library
# ============================================================================

if(TAURUS_BUILD_STATIC)
    add_library(taurus_static STATIC $<TARGET_OBJECTS:taurus_objects>)

    set_target_properties(taurus_static PROPERTIES
        OUTPUT_NAME taurus
        C_STANDARD 99
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )

    # Include directories (same as object library)
    target_include_directories(taurus_static
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/taurus/dom
            ${CMAKE_CURRENT_SOURCE_DIR}/taurus/parse
            ${CMAKE_CURRENT_SOURCE_DIR}/taurus/serialize
            ${CMAKE_CURRENT_SOURCE_DIR}/taurus/xpath
            ${CMAKE_CURRENT_SOURCE_DIR}/taurus/encoding
            ${CMAKE_CURRENT_SOURCE_DIR}/taurus/memory
            ${CMAKE_CURRENT_SOURCE_DIR}/taurus/flat
    )

    # Pull in link dependencies (utf8proc, Iconv, m) declared on
    # taurus_objects.  CMake's $<TARGET_OBJECTS:...> aggregation does NOT
    # propagate OBJECT-library link deps to the static lib's consumers
    # (CMake issue #16093), so we re-state them here as PUBLIC to ensure
    # `taurus::taurus` consumers link iconv/utf8proc/m automatically.
    target_link_libraries(taurus_static PUBLIC m)
    if(TAURUS_ENABLE_UTF8PROC)
        target_link_libraries(taurus_static PUBLIC ${TAURUS_UTF8PROC_TARGET})
    endif()
    if(TAURUS_ENABLE_ICONV)
        target_link_libraries(taurus_static PUBLIC Iconv::Iconv)
    endif()

    add_library(taurus::static ALIAS taurus_static)
    message(STATUS "Static library: ENABLED")
endif()

# ============================================================================
# Shared Library
# ============================================================================

if(TAURUS_BUILD_SHARED)
    add_library(taurus_shared SHARED $<TARGET_OBJECTS:taurus_objects>)

    # Set version properties for shared library
    # This creates: libtaurus.so.0.3.0 -> libtaurus.so.0 -> libtaurus.so
    set_target_properties(taurus_shared PROPERTIES
        VERSION ${PROJECT_VERSION}        # e.g., 0.3.0
        SOVERSION ${PROJECT_VERSION_MAJOR}  # e.g., 0
        OUTPUT_NAME taurus
        C_STANDARD 99
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )

    # Export symbols for Windows DLL
    target_compile_definitions(taurus_shared PRIVATE TAURUS_BUILDING_DLL)
    if(WIN32)
        target_compile_definitions(taurus_shared PUBLIC TAURUS_DLL)
    endif()

    # Include directories (same as object library)
    target_include_directories(taurus_shared
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/taurus/dom
            ${CMAKE_CURRENT_SOURCE_DIR}/taurus/parse
            ${CMAKE_CURRENT_SOURCE_DIR}/taurus/serialize
            ${CMAKE_CURRENT_SOURCE_DIR}/taurus/xpath
            ${CMAKE_CURRENT_SOURCE_DIR}/taurus/encoding
            ${CMAKE_CURRENT_SOURCE_DIR}/taurus/memory
            ${CMAKE_CURRENT_SOURCE_DIR}/taurus/flat
    )

    # See note on taurus_static above.
    target_link_libraries(taurus_shared PUBLIC m)
    if(TAURUS_ENABLE_UTF8PROC)
        target_link_libraries(taurus_shared PUBLIC ${TAURUS_UTF8PROC_TARGET})
    endif()
    if(TAURUS_ENABLE_ICONV)
        target_link_libraries(taurus_shared PUBLIC Iconv::Iconv)
    endif()

    add_library(taurus::shared ALIAS taurus_shared)
    message(STATUS "Shared library: ENABLED")
endif()

# ============================================================================
# Main "taurus" Target (Compatibility Alias)
# ============================================================================

# The main taurus target points to the appropriate library
if(TAURUS_BUILD_SHARED AND NOT TAURUS_BUILD_STATIC)
    # Shared only
    add_library(taurus ALIAS taurus_shared)
    add_library(taurus::taurus ALIAS taurus_shared)
elseif(TAURUS_BUILD_STATIC AND NOT TAURUS_BUILD_SHARED)
    # Static only
    add_library(taurus ALIAS taurus_static)
    add_library(taurus::taurus ALIAS taurus_static)
else()
    # Both built - default to shared for linking
    add_library(taurus ALIAS taurus_shared)
    add_library(taurus::taurus ALIAS taurus_shared)
endif()

# ============================================================================
# Installation
# ============================================================================

include(GNUInstallDirs)

# Install static library (Development component)
if(TAURUS_BUILD_STATIC)
    install(TARGETS taurus_static
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
                COMPONENT taurus_Development
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
                COMPONENT taurus_Development
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
                COMPONENT taurus_Development
    )
endif()

# Install shared library (separate Runtime and Development components)
if(TAURUS_BUILD_SHARED)
    install(TARGETS taurus_shared
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
                COMPONENT taurus_Development
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
                COMPONENT taurus_Runtime
                NAMELINK_COMPONENT taurus_Development
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
                COMPONENT taurus_Runtime
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
                COMPONENT taurus_Development
    )
endif()

# Install public headers (Development component)
install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    COMPONENT taurus_Development
    FILES_MATCHING PATTERN "*.h"
)

# Build Requires line for optional dependencies (pkg-config)
set(TAURUS_PKG_CONFIG_REQUIRES "")
if(TAURUS_ENABLE_UTF8PROC)
    set(TAURUS_PKG_CONFIG_REQUIRES "${TAURUS_PKG_CONFIG_REQUIRES} utf8proc")
endif()
if(TAURUS_ENABLE_ICONV)
    if(TAURUS_PKG_CONFIG_REQUIRES)
        set(TAURUS_PKG_CONFIG_REQUIRES "${TAURUS_PKG_CONFIG_REQUIRES}, Iconv")
    else()
        set(TAURUS_PKG_CONFIG_REQUIRES "Iconv")
    endif()
endif()
# pkg-config file (Development component)
configure_file(
    ${CMAKE_SOURCE_DIR}/cmake/taurus.pc.in
    ${CMAKE_CURRENT_BINARY_DIR}/taurus.pc
    @ONLY
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/taurus.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
    COMPONENT taurus_Development
)

# ============================================================================
# Export Targets for CMake find_package()
# ============================================================================

include(CMakePackageConfigHelpers)

# Generate the config file for find_package()
configure_package_config_file(
    ${CMAKE_SOURCE_DIR}/cmake/taurus-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/taurus-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/taurus

)

# Generate the version file
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/taurus-config-version.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

# Install CMake config files
install(
    FILES
        ${CMAKE_CURRENT_BINARY_DIR}/taurus-config.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/taurus-config-version.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/taurus
    COMPONENT taurus_Development
)
