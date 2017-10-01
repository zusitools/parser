cmake_minimum_required(VERSION 3.7)

project(parsergen)

add_executable(parsergen parsergen.cpp)
set_property(TARGET parsergen PROPERTY CXX_STANDARD 17)
set_property(TARGET parsergen PROPERTY CXX_STANDARD_REQUIRED TRUE)
target_link_libraries(parsergen pugixml stdc++fs)
