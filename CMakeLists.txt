cmake_minimum_required(VERSION 3.20)
project(rite CXX)

option(RITE_BUILD_TESTS "Build rite tests" ON)
option(RITE_BUILD_EXAMPLES "Build rite examples" ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB rite_SOURCES src/*.cpp src/http/*.cpp src/protocols/h2/*.cpp src/servers/*.cpp)

add_library(rite STATIC ${rite_SOURCES})
target_compile_options(rite PRIVATE "-std=c++23" "-g3")
target_compile_options(rite PRIVATE "-Werror" "-Wno-unused-parameter" "-Wall" "-Wextra" "-pedantic" "-Wno-missing-field-initializers")
target_include_directories(rite PUBLIC include)

set(JT_BUILD_EXAMPLES OFF)
add_subdirectory(third_party/jt)
target_link_libraries(rite PUBLIC jt)


# Link OpenSSL
target_link_libraries(rite PUBLIC "ssl" "crypto")

# Include modules
file(GLOB rite_MODULES modules/*)
foreach(module IN LISTS rite_MODULES)
  message("Adding module ${module}")
  add_subdirectory(${module})
endforeach()

if(${RITE_BUILD_EXAMPLES})
# Include examples
file(GLOB rite_EXAMPLES examples/*)
foreach(example IN LISTS rite_EXAMPLES)
  message("Adding example ${example}")
  add_subdirectory(${example})
endforeach()
endif()


# Tests
if(${RITE_BUILD_TESTS})
  add_subdirectory(third_party/googletest)
  enable_testing()

  file(GLOB rite_TESTS test/*.cpp)
  add_executable(rite-test ${rite_TESTS})
  target_link_libraries(rite-test GTest::gtest_main)
  target_link_libraries(rite-test rite)
  target_include_directories(rite-test PRIVATE include)

  include(GoogleTest)
  gtest_discover_tests(rite-test)
endif()
