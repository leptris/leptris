# C Examples for Taurus
# Demonstrates various features of the libtaurus API

# Basic example - demonstrates parsing and XPath
add_executable(basic_example basic_example.c)
target_link_libraries(basic_example PRIVATE taurus)

# XPath example - comprehensive XPath 1.0 queries
add_executable(xpath_example xpath_example.c)
target_link_libraries(xpath_example PRIVATE taurus)

# Namespace example - XML Namespaces 1.0 support
add_executable(namespace_example namespace_example.c)
target_link_libraries(namespace_example PRIVATE taurus)

# Error handling example - proper error handling patterns
add_executable(error_handling_example error_handling_example.c)
target_link_libraries(error_handling_example PRIVATE taurus)

# DOM traversal example - navigating the document tree
add_executable(dom_traversal_example dom_traversal_example.c)
target_link_libraries(dom_traversal_example PRIVATE taurus)

# Set common properties for all examples
set(EXAMPLE_TARGETS
    basic_example
    xpath_example
    namespace_example
    error_handling_example
    dom_traversal_example
)

foreach(target ${EXAMPLE_TARGETS})
    set_target_properties(${target} PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED ON
    )
endforeach()

# Add custom target to run all examples
add_custom_target(run_examples
    COMMAND echo "Running basic_example..."
    COMMAND $<TARGET_FILE:basic_example>
    COMMAND echo ""
    COMMAND echo "Running xpath_example..."
    COMMAND $<TARGET_FILE:xpath_example>
    COMMAND echo ""
    COMMAND echo "Running namespace_example..."
    COMMAND $<TARGET_FILE:namespace_example>
    COMMAND echo ""
    COMMAND echo "Running error_handling_example..."
    COMMAND $<TARGET_FILE:error_handling_example>
    COMMAND echo ""
    COMMAND echo "Running dom_traversal_example..."
    COMMAND $<TARGET_FILE:dom_traversal_example>
    DEPENDS ${EXAMPLE_TARGETS}
    COMMENT "Running all C examples"
)