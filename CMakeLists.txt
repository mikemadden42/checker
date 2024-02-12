cmake_minimum_required(VERSION 3.10)

project(checker CXX)

set (CMAKE_CXX_STANDARD 17)
if (NOT MSVC)
  set(CMAKE_CXX_FLAGS "-Wall -Wextra -pedantic")
endif()

add_executable(checker checker.cc)

if (CMAKE_SYSTEM_NAME MATCHES "Darwin")
    set_target_properties(checker PROPERTIES
        OSX_ARCHITECTURES "x86_64;arm64"
    )
endif()
