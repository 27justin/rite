cmake_minimum_required(VERSION 3.20)
project(odin-example CXX)

file(GLOB example_SOURCES src/*.cpp)
add_executable(odin-example ${example_SOURCES})
target_include_directories(odin-example PRIVATE include)

target_link_libraries(odin-example PUBLIC rite)
target_link_libraries(odin-example PUBLIC monitor)

