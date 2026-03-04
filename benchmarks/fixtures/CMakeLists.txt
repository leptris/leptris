cmake_minimum_required(VERSION 3.14)

# Fixture generator
add_executable(generate_fixtures generate_fixtures.cpp)

# Run fixture generator after build
add_custom_command(
    TARGET generate_fixtures POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_SOURCE_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_CURRENT_SOURCE_DIR}
            $<TARGET_FILE:generate_fixtures>
    COMMENT "Generating benchmark fixtures..."
    VERBATIM
)
