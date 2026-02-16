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
    commands/c14n.c
)

# Link to libtaurus
target_link_libraries(taurus-cli PRIVATE taurus)

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