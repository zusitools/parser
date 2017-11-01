cmake_minimum_required(VERSION 3.7)

project(parsergen)

add_executable(parsergen parsergen.cpp)
set_property(TARGET parsergen PROPERTY CXX_STANDARD 17)
set_property(TARGET parsergen PROPERTY CXX_STANDARD_REQUIRED TRUE)
target_link_libraries(parsergen PRIVATE pugixml stdc++ m)

if(USE_BOOST_FILESYSTEM)
  find_package(Boost COMPONENTS filesystem REQUIRED)
  target_compile_definitions(parsergen PRIVATE -DUSE_BOOST_FILESYSTEM)
  target_link_libraries(parsergen PRIVATE ${Boost_LIBRARIES})
  target_include_directories(parsergen PRIVATE ${Boost_INCLUDE_DIRS})
else()
  target_link_libraries(parsergen PRIVATE stdc++fs)
endif()
