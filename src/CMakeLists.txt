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
    # DOM nodes
    taurus/dom/node.c
    taurus/dom/element.c
    taurus/dom/element_modify.c
    taurus/dom/element_copy.c
    taurus/dom/element_text.c
    taurus/dom/element_namespace.c
    taurus/dom/text.c
    taurus/dom/comment.c
    taurus/dom/cdata.c
    taurus/dom/pi.c
    taurus/dom/doctype.c

    # Memory management
    taurus/memory/pool.c
    taurus/memory/compact_allocator.c
    taurus/memory/compact_single_alloc.c
    taurus/dom/compact.c
    taurus/dom/compact_accessor.c
    taurus/taurus_memory.c

    # Common utilities
    taurus/common/string_view.c
    taurus/common/entities.c

    # Encoding (UTF-16 support - always available)
    taurus/encoding/utf16.c
    taurus/encoding/unicode.c

    # Parser and Serializer
    taurus/parse/parser.c
    taurus/parse/parser_size.c
    taurus/parse/ptr_parser.c
    # NOTE: Legacy parsers archived in archive/parser_versions/
    # parser.c is the v5 compact-only parser (1.18x vs pugixml)
    # ptr_parser.c is the pointer-based parser (1.29-1.45x vs pugixml)
    taurus/serialize/serialize.c

    # XPath
    taurus/xpath/lexer.c
    taurus/xpath/parser.c
    taurus/xpath/evaluator.c
    taurus/xpath/evaluator_types.c
    taurus/xpath/evaluator_axes.c
    taurus/xpath/evaluator_operators.c
    taurus/xpath/evaluator_path.c
    taurus/xpath/functions.c
    taurus/xpath/functions_registry.c
    taurus/xpath/functions_core.c
    taurus/xpath/functions_boolean.c
    taurus/xpath/functions_number.c
    taurus/xpath/functions_string.c
    taurus/xpath/functions_nodeset.c
    taurus/xpath/xpath_variables.c
    taurus/xpath/expr_cache.c
    taurus/xpath/xpath_fast_path.c

    # SAX
    taurus/sax/parser.c

    # DTD
    taurus/dtd/model.c
    taurus/dtd/parser.c
    taurus/dtd/resolver.c
    # taurus/dtd/validator.c # TODO: Phase 5/6 - Validation layer

    # XInclude - TODO: Update for compact architecture
    # taurus/xinclude/xinclude.c

    # Core library (split into focused modules)
    taurus/taurus_api.c
    taurus/taurus_document.c
    taurus/taurus_parse_api.c
    taurus/taurus_element_api.c
    taurus/taurus_xpath_api.c
    taurus/taurus_c14n.c
    taurus/taurus_node_api.c
    taurus/error.c
    taurus/allocator.c
    taurus/observer.c
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
)

target_link_libraries(taurus_objects PRIVATE m)

target_compile_options(taurus_objects PRIVATE
    -Wall
    -Wextra
    -Wno-unused-parameter
    $<$<CONFIG:Release>:-O3>
    $<$<CONFIG:Release>:-flto>
    $<$<CONFIG:Release>:-mcpu=apple-m1>
    $<$<CONFIG:Release>:-ffast-math>
    $<$<CONFIG:Release>:-funroll-loops>
    $<$<CONFIG:Debug>:-g -O0>
)

# Link-time optimization for release builds
target_link_options(taurus_objects PRIVATE
    $<$<CONFIG:Release>:-flto>
)

# ============================================================================
# Optional Dependencies
# ============================================================================

# utf8proc for Unicode support
if(TAURUS_ENABLE_UTF8PROC)
    find_package(unofficial-utf8proc CONFIG REQUIRED)

    target_link_libraries(taurus_objects PRIVATE utf8proc)
    target_compile_definitions(taurus_objects PRIVATE TAURUS_HAS_UTF8PROC=1)

    target_sources(taurus_objects PRIVATE
        taurus/unicode/unicode.c
    )

    target_include_directories(taurus_objects PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/taurus/unicode
    )

    message(STATUS "utf8proc: ENABLED")
else()
    message(STATUS "utf8proc: DISABLED")
endif()

# iconv for encoding conversion
if(TAURUS_ENABLE_ICONV)
    find_package(Iconv REQUIRED)

    if(Iconv_FOUND)
        target_link_libraries(taurus_objects PRIVATE Iconv::Iconv)
        target_compile_definitions(taurus_objects PRIVATE TAURUS_HAS_ICONV=1)

        target_sources(taurus_objects PRIVATE
            taurus/encoding/encoding.c
        )

        target_include_directories(taurus_objects PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/taurus/encoding
        )

        message(STATUS "iconv: ENABLED")
    else()
        message(FATAL_ERROR "iconv: NOT FOUND (required)")
    endif()
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
    )

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
    )

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
        EXPORT taurus-targets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
                COMPONENT taurus_Development
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
                COMPONENT taurus_Development
    )
endif()

# Install shared library (separate Runtime and Development components)
if(TAURUS_BUILD_SHARED)
    install(TARGETS taurus_shared
        EXPORT taurus-targets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
                COMPONENT taurus_Development
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
                COMPONENT taurus_Runtime
                NAMELINK_COMPONENT taurus_Development
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
                COMPONENT taurus_Runtime
    )
endif()

# Export targets for find_package()
install(EXPORT taurus-targets
    NAMESPACE taurus::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/taurus
    COMPONENT taurus_Development
)

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
