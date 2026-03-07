# test/c/threading/CMakeLists.txt - Threading tests for per-document concurrency

# Threading concurrent parse test
add_executable(test_threading_concurrent_parse test_threading_concurrent_parse.cc)
target_link_libraries(test_threading_concurrent_parse
    taurus
    GTest::gtest
    GTest::gtest_main
    pthread
)
target_include_directories(test_threading_concurrent_parse PRIVATE
    ${CMAKE_SOURCE_DIR}/src/include
    ${CMAKE_SOURCE_DIR}/src/taurus
)

# Add to ctest
add_test(NAME test_threading_concurrent_parse COMMAND test_threading_concurrent_parse)
set_tests_properties(test_threading_concurrent_parse PROPERTIES
    LABELS "threading;unit"
)
