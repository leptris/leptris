# Taurus CLI executable

# CLI executable
add_executable(taurus-cli
    main.c
    cli.c
    options.c
    output.c
    error.c
    commands/version.c
    commands/parse.c
    commands/xpath.c
    commands/format.c
)

# Link to libtaurus. When both static and shared are built, link the
# CLI against the STATIC library: the CLI's output formatters use
# internal accessors (taurus_element_get_name, compact-pointer
# decoding, struct taurus_attribute walks) that are not exported
# from the shared library, so a shared-only link fails. TODO 191
# tracks moving the CLI onto the public API only; until then this
# keeps TAURUS_BUILD_SHARED=ON fully buildable.
if(TARGET taurus_static)
    target_link_libraries(taurus-cli PRIVATE taurus_static)
else()
    target_link_libraries(taurus-cli PRIVATE taurus)
endif()

# Include directories
target_include_directories(taurus-cli PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/src/include
)

# Set output name (without -cli suffix)
set_target_properties(taurus-cli PROPERTIES
    OUTPUT_NAME taurus
)

# Install
install(TARGETS taurus-cli
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)