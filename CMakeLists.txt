cmake_minimum_required(VERSION 3.2)

project(parser)

add_subdirectory(pugixml)
add_subdirectory(parsergen)

set(xsd_sources
  ${CMAKE_CURRENT_SOURCE_DIR}/xsd/authority.xml.xsd
  ${CMAKE_CURRENT_SOURCE_DIR}/xsd/author.xml.xsd
  ${CMAKE_CURRENT_SOURCE_DIR}/xsd/font.xml.xsd
  ${CMAKE_CURRENT_SOURCE_DIR}/xsd/forest.xml.xsd
  ${CMAKE_CURRENT_SOURCE_DIR}/xsd/fpn.xsd
  ${CMAKE_CURRENT_SOURCE_DIR}/xsd/ftd.xsd
  ${CMAKE_CURRENT_SOURCE_DIR}/xsd/gpa.xml.xsd
  ${CMAKE_CURRENT_SOURCE_DIR}/xsd/input.xml.xsd
  ${CMAKE_CURRENT_SOURCE_DIR}/xsd/levelcrossing.xml.xsd
  ${CMAKE_CURRENT_SOURCE_DIR}/xsd/ls3.xsd
  ${CMAKE_CURRENT_SOURCE_DIR}/xsd/material.xml.xsd
  ${CMAKE_CURRENT_SOURCE_DIR}/xsd/material.xsd
  ${CMAKE_CURRENT_SOURCE_DIR}/xsd/platform.xml.xsd
  ${CMAKE_CURRENT_SOURCE_DIR}/xsd/result.xml.xsd
  ${CMAKE_CURRENT_SOURCE_DIR}/xsd/retainingwall.xml.xsd
  ${CMAKE_CURRENT_SOURCE_DIR}/xsd/root_alle_zusi_dateien.xsd
  ${CMAKE_CURRENT_SOURCE_DIR}/xsd/root_authority.xml.xsd
  ${CMAKE_CURRENT_SOURCE_DIR}/xsd/root_author.xml.xsd
  ${CMAKE_CURRENT_SOURCE_DIR}/xsd/root_font.xml.xsd
  ${CMAKE_CURRENT_SOURCE_DIR}/xsd/root_forest.xml.xsd
  ${CMAKE_CURRENT_SOURCE_DIR}/xsd/root_fpn.xsd
  ${CMAKE_CURRENT_SOURCE_DIR}/xsd/root_ftd.xsd
  ${CMAKE_CURRENT_SOURCE_DIR}/xsd/root_gpa.xml.xsd
  ${CMAKE_CURRENT_SOURCE_DIR}/xsd/root_input.xml.xsd
  ${CMAKE_CURRENT_SOURCE_DIR}/xsd/root_levelcrossing.xml.xsd
  ${CMAKE_CURRENT_SOURCE_DIR}/xsd/root_ls3.xsd
  ${CMAKE_CURRENT_SOURCE_DIR}/xsd/root_material.xml.xsd
  ${CMAKE_CURRENT_SOURCE_DIR}/xsd/root_platform.xml.xsd
  ${CMAKE_CURRENT_SOURCE_DIR}/xsd/root_result.xml.xsd
  ${CMAKE_CURRENT_SOURCE_DIR}/xsd/root_retainingwall.xml.xsd
  ${CMAKE_CURRENT_SOURCE_DIR}/xsd/root_rv.fzg.xsd
  ${CMAKE_CURRENT_SOURCE_DIR}/xsd/root_script.xml.xsd
  ${CMAKE_CURRENT_SOURCE_DIR}/xsd/root_shape.xml.xsd
  ${CMAKE_CURRENT_SOURCE_DIR}/xsd/root_signals.xml.xsd
  ${CMAKE_CURRENT_SOURCE_DIR}/xsd/root_signal.xml.xsd
  ${CMAKE_CURRENT_SOURCE_DIR}/xsd/root_st2.xsd
  ${CMAKE_CURRENT_SOURCE_DIR}/xsd/root_st3.xsd
  ${CMAKE_CURRENT_SOURCE_DIR}/xsd/root_timetable.xml.xsd
  ${CMAKE_CURRENT_SOURCE_DIR}/xsd/root_travellers.xsd
  ${CMAKE_CURRENT_SOURCE_DIR}/xsd/root_trn.xml.xsd
  ${CMAKE_CURRENT_SOURCE_DIR}/xsd/root_trn.xsd
  ${CMAKE_CURRENT_SOURCE_DIR}/xsd/root_turnout.xml.xsd
  ${CMAKE_CURRENT_SOURCE_DIR}/xsd/root_zusi2files.xml.xsd
  ${CMAKE_CURRENT_SOURCE_DIR}/xsd/rv.fzg.xsd
  ${CMAKE_CURRENT_SOURCE_DIR}/xsd/script.xml.xsd
  ${CMAKE_CURRENT_SOURCE_DIR}/xsd/shape.xml.xsd
  ${CMAKE_CURRENT_SOURCE_DIR}/xsd/signals.xml.xsd
  ${CMAKE_CURRENT_SOURCE_DIR}/xsd/signal.xml.xsd
  ${CMAKE_CURRENT_SOURCE_DIR}/xsd/st2.xsd
  ${CMAKE_CURRENT_SOURCE_DIR}/xsd/st3.xsd
  ${CMAKE_CURRENT_SOURCE_DIR}/xsd/timetable.xml.xsd
  ${CMAKE_CURRENT_SOURCE_DIR}/xsd/travellers.xml.xsd
  ${CMAKE_CURRENT_SOURCE_DIR}/xsd/trn.xsd
  ${CMAKE_CURRENT_SOURCE_DIR}/xsd/typen.xsd
  ${CMAKE_CURRENT_SOURCE_DIR}/xsd/zusi2files.xml.xsd
  ${CMAKE_CURRENT_SOURCE_DIR}/xsd/zusi.xsd
)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/include/zusi_parser")
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/include/zusi_parser/zusi_types.hpp ${CMAKE_CURRENT_BINARY_DIR}/include/zusi_parser/zusi_parser.hpp ${CMAKE_CURRENT_BINARY_DIR}/include/zusi_parser/zusi_parser_fwd.hpp
  # COMMAND parsergen "${CMAKE_CURRENT_SOURCE_DIR}/xsd/root_alle_zusi_dateien.xsd" "${CMAKE_CURRENT_BINARY_DIR}/include/zusi_parser"
  COMMAND parsergen "${CMAKE_CURRENT_SOURCE_DIR}/xsd/root_ls3.xsd" "${CMAKE_CURRENT_BINARY_DIR}/include/zusi_parser"
  DEPENDS ${xsd_sources} parsergen)

add_custom_target(zusi_parser_includes SOURCES ${CMAKE_CURRENT_BINARY_DIR}/include/zusi_parser/zusi_types.hpp ${CMAKE_CURRENT_BINARY_DIR}/include/zusi_parser/zusi_parser.hpp ${CMAKE_CURRENT_BINARY_DIR}/include/zusi_parser/zusi_parser_fwd.hpp)

add_library(zusi_parser INTERFACE)
target_include_directories(zusi_parser INTERFACE ${CMAKE_CURRENT_BINARY_DIR}/include include rapidxml-mod)
add_dependencies(zusi_parser INTERFACE zusi_parser_includes)

add_executable(benchmark benchmark/benchmark-parse.cpp)
target_link_libraries(benchmark PRIVATE zusi_parser)
target_compile_options(benchmark PRIVATE -Wall -Wextra)
target_compile_definitions(benchmark PRIVATE -DUSE_MMAP)
set_property(TARGET benchmark PROPERTY CXX_STANDARD 17)
set_property(TARGET benchmark PROPERTY CXX_STANDARD_REQUIRED TRUE)
target_link_libraries(benchmark PRIVATE stdc++)
