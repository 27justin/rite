cmake_minimum_required(VERSION 3.20)
project(rite CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB rite_SOURCES src/*.cpp src/http/*.cpp src/protocols/h2/*.cpp src/servers/*.cpp)

add_library(rite STATIC ${rite_SOURCES})
target_compile_options(rite PUBLIC "-std=c++23" "-g3")
target_compile_options(rite PUBLIC "-Werror" "-Wno-unused-parameter" "-Wall" "-Wextra" "-pedantic" "-Wno-missing-field-initializers")
target_include_directories(rite PUBLIC include)

set(JT_BUILD_EXAMPLES OFF)
add_subdirectory(third_party/jt)
target_link_libraries(rite PUBLIC jt)


# Link OpenSSL
target_link_libraries(rite PUBLIC "ssl" "crypto")


# Include modules
file(GLOB rite_MODULES modules/*)
foreach(module IN LISTS rite_MODULES)
  message(INFO "Adding module ${module}")
  add_subdirectory(${module})
endforeach()

# Include examples
file(GLOB rite_EXAMPLES examples/*)
foreach(example IN LISTS rite_EXAMPLES)
  message(INFO "Adding example ${example}")
  add_subdirectory(${example})
endforeach()

