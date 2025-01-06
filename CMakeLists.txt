cmake_minimum_required(VERSION 3.20)

project(kana CXX)

file(GLOB kana_SOURCES src/*.cpp src/http/*.cpp)

add_library(kana STATIC ${kana_SOURCES})
target_compile_options(kana PUBLIC "-std=c++23")
target_include_directories(kana PUBLIC include)

set(JT_BUILD_EXAMPLES OFF)
add_subdirectory(third_party/jt)
target_link_libraries(kana PUBLIC jt)

# Include modules

file(GLOB kana_MODULES modules/*)
foreach(module IN LISTS kana_MODULES)
  message(INFO "Adding module ${module}")
  add_subdirectory(${module})
endforeach()

# Include examples
file(GLOB kana_EXAMPLES examples/*)
foreach(example IN LISTS kana_EXAMPLES)
  message(INFO "Adding example ${example}")
  add_subdirectory(${example})
endforeach()

