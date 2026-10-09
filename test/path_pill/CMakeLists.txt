cmake_minimum_required(VERSION 3.16)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(path_pill NONE)
  enable_testing()
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME PathPill.Layout
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run.py"
          --work "${CMAKE_CURRENT_BINARY_DIR}/harness")
