cmake_minimum_required(VERSION 3.20)
project(leptris_test_package LANGUAGES C)

find_package(leptris CONFIG REQUIRED)

add_executable(smoke src/example.c)
target_link_libraries(smoke PRIVATE leptris::leptris)
