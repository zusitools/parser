cmake_minimum_required(VERSION 3.2)

project(zusi_parser)

# parsergen
# (has to be in the same CMakeLists.txt as add_custom_command)
if(CMAKE_CROSSCOMPILING)
  set(IMPORT_EXECUTABLES "IMPORTFILE-NOTFOUND" CACHE FILEPATH "Point it to the export file from a native build")
  include(${IMPORT_EXECUTABLES})
else()
  add_subdirectory(pugixml)
  add_executable(parsergen parsergen/parsergen.cpp)
  set_property(TARGET parsergen PROPERTY CXX_STANDARD 17)
  set_property(TARGET parsergen PROPERTY CXX_STANDARD_REQUIRED TRUE)
  target_link_libraries(parsergen PRIVATE pugixml stdc++ m)

  find_package(Boost COMPONENTS program_options REQUIRED)
  target_link_libraries(parsergen PRIVATE Boost::program_options)

  if(ZUSI_PARSER_USE_BOOST_FILESYSTEM)
    find_package(Boost COMPONENTS filesystem REQUIRED)
    target_compile_definitions(parsergen PRIVATE -DZUSI_PARSER_USE_BOOST_FILESYSTEM)
    target_link_libraries(parsergen PRIVATE Boost::filesystem)
  else()
    target_link_libraries(parsergen PRIVATE $<$<AND:$<CXX_COMPILER_ID:GNU>,$<VERSION_LESS:$<CXX_COMPILER_VERSION>,9.0>>:stdc++fs>)
  endif()

  export(TARGETS parsergen FILE ${CMAKE_BINARY_DIR}/ImportExecutables.cmake )
endif()

set(ZUSI_PARSER_SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR} CACHE INTERNAL "")

function(generate_zusi_parser targetName outputDir)
  cmake_parse_arguments(PARSE_ARGV 2 GENERATE_ZUSI_PARSER "IGNORE_UNKNOWN" "" "WHITELIST")
  set(xsd_sources
    ${ZUSI_PARSER_SOURCE_DIR}/xsd/authority.xml.xsd
    ${ZUSI_PARSER_SOURCE_DIR}/xsd/author.xml.xsd
    ${ZUSI_PARSER_SOURCE_DIR}/xsd/font.xml.xsd
    ${ZUSI_PARSER_SOURCE_DIR}/xsd/forest.xml.xsd
    ${ZUSI_PARSER_SOURCE_DIR}/xsd/fpn.xsd
    ${ZUSI_PARSER_SOURCE_DIR}/xsd/ftd.xsd
    ${ZUSI_PARSER_SOURCE_DIR}/xsd/gpa.xml.xsd
    ${ZUSI_PARSER_SOURCE_DIR}/xsd/input.xml.xsd
    ${ZUSI_PARSER_SOURCE_DIR}/xsd/levelcrossing.xml.xsd
    ${ZUSI_PARSER_SOURCE_DIR}/xsd/ls3.xsd
    ${ZUSI_PARSER_SOURCE_DIR}/xsd/material.xml.xsd
    ${ZUSI_PARSER_SOURCE_DIR}/xsd/material.xsd
    ${ZUSI_PARSER_SOURCE_DIR}/xsd/platform.xml.xsd
    ${ZUSI_PARSER_SOURCE_DIR}/xsd/result.xml.xsd
    ${ZUSI_PARSER_SOURCE_DIR}/xsd/retainingwall.xml.xsd
    ${ZUSI_PARSER_SOURCE_DIR}/xsd/root_alle_zusi_dateien.xsd
    ${ZUSI_PARSER_SOURCE_DIR}/xsd/root_authority.xml.xsd
    ${ZUSI_PARSER_SOURCE_DIR}/xsd/root_author.xml.xsd
    ${ZUSI_PARSER_SOURCE_DIR}/xsd/root_font.xml.xsd
    ${ZUSI_PARSER_SOURCE_DIR}/xsd/root_forest.xml.xsd
    ${ZUSI_PARSER_SOURCE_DIR}/xsd/root_fpn.xsd
    ${ZUSI_PARSER_SOURCE_DIR}/xsd/root_ftd.xsd
    ${ZUSI_PARSER_SOURCE_DIR}/xsd/root_gpa.xml.xsd
    ${ZUSI_PARSER_SOURCE_DIR}/xsd/root_input.xml.xsd
    ${ZUSI_PARSER_SOURCE_DIR}/xsd/root_levelcrossing.xml.xsd
    ${ZUSI_PARSER_SOURCE_DIR}/xsd/root_ls3.xsd
    ${ZUSI_PARSER_SOURCE_DIR}/xsd/root_material.xml.xsd
    ${ZUSI_PARSER_SOURCE_DIR}/xsd/root_platform.xml.xsd
    ${ZUSI_PARSER_SOURCE_DIR}/xsd/root_result.xml.xsd
    ${ZUSI_PARSER_SOURCE_DIR}/xsd/root_retainingwall.xml.xsd
    ${ZUSI_PARSER_SOURCE_DIR}/xsd/root_rv.fzg.xsd
    ${ZUSI_PARSER_SOURCE_DIR}/xsd/root_script.xml.xsd
    ${ZUSI_PARSER_SOURCE_DIR}/xsd/root_shape.xml.xsd
    ${ZUSI_PARSER_SOURCE_DIR}/xsd/root_signals.xml.xsd
    ${ZUSI_PARSER_SOURCE_DIR}/xsd/root_signal.xml.xsd
    ${ZUSI_PARSER_SOURCE_DIR}/xsd/root_st2.xsd
    ${ZUSI_PARSER_SOURCE_DIR}/xsd/root_st3.xsd
    ${ZUSI_PARSER_SOURCE_DIR}/xsd/root_timetable.xml.xsd
    ${ZUSI_PARSER_SOURCE_DIR}/xsd/root_travellers.xsd
    ${ZUSI_PARSER_SOURCE_DIR}/xsd/root_trn.xml.xsd
    ${ZUSI_PARSER_SOURCE_DIR}/xsd/root_trn.xsd
    ${ZUSI_PARSER_SOURCE_DIR}/xsd/root_turnout.xml.xsd
    ${ZUSI_PARSER_SOURCE_DIR}/xsd/root_zusi2files.xml.xsd
    ${ZUSI_PARSER_SOURCE_DIR}/xsd/rv.fzg.xsd
    ${ZUSI_PARSER_SOURCE_DIR}/xsd/script.xml.xsd
    ${ZUSI_PARSER_SOURCE_DIR}/xsd/shape.xml.xsd
    ${ZUSI_PARSER_SOURCE_DIR}/xsd/signals.xml.xsd
    ${ZUSI_PARSER_SOURCE_DIR}/xsd/signal.xml.xsd
    ${ZUSI_PARSER_SOURCE_DIR}/xsd/st2.xsd
    ${ZUSI_PARSER_SOURCE_DIR}/xsd/st3.xsd
    ${ZUSI_PARSER_SOURCE_DIR}/xsd/timetable.xml.xsd
    ${ZUSI_PARSER_SOURCE_DIR}/xsd/travellers.xml.xsd
    ${ZUSI_PARSER_SOURCE_DIR}/xsd/trn.xsd
    ${ZUSI_PARSER_SOURCE_DIR}/xsd/typen.xsd
    ${ZUSI_PARSER_SOURCE_DIR}/xsd/zusi2files.xml.xsd
    ${ZUSI_PARSER_SOURCE_DIR}/xsd/zusi.xsd
  )

  set(generate_args "")
  if (GENERATE_ZUSI_PARSER_IGNORE_UNKNOWN)
    set(generate_args "${generate_args};--ignore-unknown")
  endif()
  foreach (whitelist_entry IN LISTS GENERATE_ZUSI_PARSER_WHITELIST)
    set(generate_args "${generate_args};--whitelist;${whitelist_entry}")
  endforeach()
  add_custom_command(OUTPUT "${outputDir}/zusi_parser/zusi_types.hpp" "${outputDir}/zusi_parser/zusi_types_fwd.hpp" "${outputDir}/zusi_parser/zusi_parser.hpp" "${outputDir}/zusi_parser/zusi_parser_fwd.hpp"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${outputDir}/zusi_parser"
    COMMAND parsergen
        --xsd "${ZUSI_PARSER_SOURCE_DIR}/xsd/root_alle_zusi_dateien.xsd"
        --out-dir "${outputDir}/zusi_parser"
        ${generate_args}
    COMMAND_EXPAND_LISTS
    VERBATIM
    DEPENDS ${xsd_sources} parsergen)

  add_custom_target(${targetName}_includes SOURCES "${outputDir}/zusi_parser/zusi_types.hpp" "${outputDir}/zusi_parser/zusi_parser.hpp" "${outputDir}/zusi_parser/zusi_parser_fwd.hpp")

  add_library(${targetName} INTERFACE)
  target_include_directories(${targetName} INTERFACE "${outputDir}" "${ZUSI_PARSER_SOURCE_DIR}/include" "${ZUSI_PARSER_SOURCE_DIR}/rapidxml-mod")
  add_dependencies(${targetName} INTERFACE ${targetName}_includes)
endfunction()
