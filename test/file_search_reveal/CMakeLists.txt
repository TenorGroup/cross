if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.16)
  project(FileSearchRevealTests LANGUAGES CXX)
  enable_testing()
  get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
endif()

add_executable(FileSearchRevealTest FileSearchRevealTest.cpp)
target_compile_features(FileSearchRevealTest PRIVATE cxx_std_17)
target_compile_options(FileSearchRevealTest PRIVATE -Wall -Wextra -pedantic)
target_include_directories(FileSearchRevealTest PRIVATE ${REPO_ROOT}/src)
add_test(NAME FileSearchReveal.logic COMMAND FileSearchRevealTest)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(
  NAME FileSearchReveal.production
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/production.py"
          --header "${REPO_ROOT}/src/activities/home/FileBrowserActivity.h"
)
