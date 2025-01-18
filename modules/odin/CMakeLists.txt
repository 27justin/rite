cmake_minimum_required(VERSION 3.20)
project(monitor CXX)

file(GLOB monitor_SOURCES src/*.cpp)

add_library(monitor STATIC ${monitor_SOURCES})
target_include_directories(monitor PUBLIC include)
target_link_libraries(monitor PUBLIC rite)

