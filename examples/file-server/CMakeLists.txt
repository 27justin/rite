cmake_minimum_required(VERSION 3.20)
project(staticfile-example CXX)

# GitHub CI runners do not have libmagic, thus do not compile on there.
if(NOT GITHUB_CI)
  file(GLOB example_SOURCES src/*.cpp)
  add_executable(staticfile-example ${example_SOURCES})
  target_include_directories(staticfile-example PRIVATE include)

  target_link_libraries(staticfile-example PUBLIC rite)
  target_link_libraries(staticfile-example PUBLIC session)
  target_link_libraries(staticfile-example PUBLIC magic)
endif()

