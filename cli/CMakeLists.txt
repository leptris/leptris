# Leptris CLI executable

# CLI executable
add_executable(leptris-cli
    main.c
    cli.c
    options.c
    output.c
    error.c
    commands/version.c
    commands/parse.c
    commands/xpath.c
    commands/format.c
    commands/xquery.c
)

# Link to libleptris. When both static and shared are built, link the
# CLI against the STATIC library: the CLI's output formatters use
# internal accessors (leptris_element_get_name, compact-pointer
# decoding, struct leptris_attribute walks) that are not exported
# from the shared library, so a shared-only link fails. TODO 191
# tracks moving the CLI onto the public API only; until then this
# keeps LEPTRIS_BUILD_SHARED=ON fully buildable.
if(TARGET leptris_static)
    target_link_libraries(leptris-cli PRIVATE leptris_static)
else()
    target_link_libraries(leptris-cli PRIVATE leptris)
endif()

# Include directories
target_include_directories(leptris-cli PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/src/include
)

# Set output name (without -cli suffix)
set_target_properties(leptris-cli PROPERTIES
    OUTPUT_NAME leptris
)

# Install
install(TARGETS leptris-cli
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)