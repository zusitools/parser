generate_zusi_parser(zusi_parser ${CMAKE_CURRENT_BINARY_DIR}/zusi_parser)

find_package(Boost COMPONENTS unit_test_framework REQUIRED)
add_executable(parser_test
  main.cpp
  zusi_pfad_test.cpp)
set_property(TARGET parser_test PROPERTY CXX_STANDARD 17)
set_property(TARGET parser_test PROPERTY CXX_STANDARD_REQUIRED TRUE)
target_compile_options(parser_test PRIVATE -DBOOST_TEST_DYN_LINK)
target_link_libraries(parser_test Boost::unit_test_framework)
target_link_libraries(parser_test zusi_parser)
add_test(parser_test COMMAND parser_test --show_progress)
