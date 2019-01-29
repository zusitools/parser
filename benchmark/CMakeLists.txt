cmake_minimum_required(VERSION 3.2)

project(zusi_parser_benchmark)

add_subdirectory(.. parser)

generate_zusi_parser(zusi_parser ${CMAKE_CURRENT_BINARY_DIR}/zusi_parser)

add_executable(benchmark benchmark-parse.cpp)
target_link_libraries(benchmark PRIVATE zusi_parser)
target_compile_options(benchmark PRIVATE -Wall -Wextra)
target_compile_definitions(benchmark PRIVATE -DUSE_MMAP)
set_property(TARGET benchmark PROPERTY CXX_STANDARD 17)
set_property(TARGET benchmark PROPERTY CXX_STANDARD_REQUIRED TRUE)
target_link_libraries(benchmark PRIVATE stdc++)
