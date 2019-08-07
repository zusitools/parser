cmake_minimum_required(VERSION 3.2)

project(zusi_parser_test)

add_subdirectory(.. parser)

generate_zusi_parser(zusi_parser ${CMAKE_CURRENT_BINARY_DIR}/zusi_parser)

find_package(Boost COMPONENTS unit_test_framework REQUIRED)
add_executable(parser_test
  main.cpp
  zusi_pfad_test.cpp)
set_property(TARGET parser_test PROPERTY CXX_STANDARD 17)
set_property(TARGET parser_test PROPERTY CXX_STANDARD_REQUIRED TRUE)
if(UNIX)
  target_compile_options(parser_test PRIVATE -DBOOST_TEST_DYN_LINK)
endif()
target_link_libraries(parser_test PRIVATE Boost::unit_test_framework)
target_link_libraries(parser_test PRIVATE zusi_parser)

if(ZUSI_PARSER_USE_BOOST_FILESYSTEM)
  find_package(Boost COMPONENTS filesystem REQUIRED)
  target_compile_definitions(parser_test PRIVATE -DZUSI_PARSER_USE_BOOST_FILESYSTEM)
  target_link_libraries(parser_test PRIVATE Boost::filesystem)
else()
  target_link_libraries(parser_test PRIVATE stdc++fs)
endif()

add_test(parser_test COMMAND parser_test --show_progress)
