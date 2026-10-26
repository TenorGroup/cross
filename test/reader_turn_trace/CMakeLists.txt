cmake_minimum_required(VERSION 3.16)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(reader_turn_trace_tests CXX)
  enable_testing()
  get_filename_component(REPO_ROOT "../.." ABSOLUTE)
endif()
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME reader_turn_trace_projection
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run_tests.py"
          "${REPO_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/production-projection"
          --compiler "${CMAKE_CXX_COMPILER}")
set_tests_properties(reader_turn_trace_projection PROPERTIES TIMEOUT 60)
add_test(NAME reader_side_hold_input
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_side_hold_input.py")
set_tests_properties(reader_side_hold_input PROPERTIES TIMEOUT 60)
