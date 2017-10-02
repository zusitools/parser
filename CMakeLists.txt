cmake_minimum_required(VERSION 3.2)

project(parser)

add_subdirectory(pugixml)
add_subdirectory(parsergen)

set(xsd_sources
  xsd/authority.xml.xsd
  xsd/author.xml.xsd
  xsd/font.xml.xsd
  xsd/forest.xml.xsd
  xsd/fpn.xsd
  xsd/ftd.xsd
  xsd/gpa.xml.xsd
  xsd/input.xml.xsd
  xsd/levelcrossing.xml.xsd
  xsd/ls3.xsd
  xsd/material.xml.xsd
  xsd/material.xsd
  xsd/platform.xml.xsd
  xsd/result.xml.xsd
  xsd/retainingwall.xml.xsd
  xsd/root_alle_zusi_dateien.xsd
  xsd/root_authority.xml.xsd
  xsd/root_author.xml.xsd
  xsd/root_font.xml.xsd
  xsd/root_forest.xml.xsd
  xsd/root_fpn.xsd
  xsd/root_ftd.xsd
  xsd/root_gpa.xml.xsd
  xsd/root_input.xml.xsd
  xsd/root_levelcrossing.xml.xsd
  xsd/root_ls3.xsd
  xsd/root_material.xml.xsd
  xsd/root_platform.xml.xsd
  xsd/root_result.xml.xsd
  xsd/root_retainingwall.xml.xsd
  xsd/root_rv.fzg.xsd
  xsd/root_script.xml.xsd
  xsd/root_shape.xml.xsd
  xsd/root_signals.xml.xsd
  xsd/root_signal.xml.xsd
  xsd/root_st2.xsd
  xsd/root_st3.xsd
  xsd/root_timetable.xml.xsd
  xsd/root_travellers.xsd
  xsd/root_trn.xml.xsd
  xsd/root_trn.xsd
  xsd/root_turnout.xml.xsd
  xsd/root_zusi2files.xml.xsd
  xsd/rv.fzg.xsd
  xsd/script.xml.xsd
  xsd/shape.xml.xsd
  xsd/signals.xml.xsd
  xsd/signal.xml.xsd
  xsd/st2.xsd
  xsd/st3.xsd
  xsd/timetable.xml.xsd
  xsd/travellers.xml.xsd
  xsd/trn.xsd
  xsd/typen.xsd
  xsd/zusi2files.xml.xsd
  xsd/zusi.xsd
)
add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/zusi_types.hpp ${CMAKE_BINARY_DIR}/zusi_parser.hpp ${CMAKE_BINARY_DIR}/zusi_parser_fwd.hpp
  COMMAND parsergen "${CMAKE_SOURCE_DIR}/xsd/root_alle_zusi_dateien.xsd" "${CMAKE_BINARY_DIR}"
  DEPENDS ${xsd_sources} parsergen)

add_executable(benchmark benchmark/benchmark-parse.cpp ${CMAKE_BINARY_DIR}/zusi_types.hpp ${CMAKE_BINARY_DIR}/zusi_parser.hpp ${CMAKE_BINARY_DIR}/zusi_parser_fwd.hpp)
target_include_directories(benchmark PRIVATE ${CMAKE_BINARY_DIR} rapidxml-mod)
target_compile_options(benchmark PRIVATE -Wall -Wextra)
