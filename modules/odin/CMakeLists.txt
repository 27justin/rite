cmake_minimum_required(VERSION 3.20)
project(kana-odin CXX)

file(GLOB odin_SOURCES src/*.cpp)
add_library(kana-odin STATIC ${odin_SOURCES})
target_include_directories(kana-odin PUBLIC include)
target_link_libraries(kana-odin PUBLIC kana)

