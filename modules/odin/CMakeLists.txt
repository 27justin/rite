cmake_minimum_required(VERSION 3.20)
project(rite-odin CXX)

file(GLOB odin_SOURCES src/*.cpp)
add_library(rite-odin STATIC ${odin_SOURCES})
target_include_directories(rite-odin PUBLIC include)
target_link_libraries(rite-odin PUBLIC rite)

