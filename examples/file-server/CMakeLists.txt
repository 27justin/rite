cmake_minimum_required(VERSION 3.20)
project(staticfile-example CXX)

file(GLOB example_SOURCES src/*.cpp)
add_executable(staticfile-example ${example_SOURCES})
target_include_directories(staticfile-example PRIVATE include)

target_link_libraries(staticfile-example PUBLIC rite)

