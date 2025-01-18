cmake_minimum_required(VERSION 3.20)
project(session CXX)

add_library(session src/session.cpp)
target_include_directories(session PUBLIC include)

target_link_libraries(session PUBLIC rite)

