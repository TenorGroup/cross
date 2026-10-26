cmake_minimum_required(VERSION 3.16)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(end_of_book_jump_tests CXX)
  enable_testing()
  get_filename_component(REPO_ROOT "../.." ABSOLUTE)
endif()
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME end_of_book_jump_projection
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run_tests.py"
          "${REPO_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/production-projection"
          --compiler "${CMAKE_CXX_COMPILER}")
set_tests_properties(end_of_book_jump_projection PROPERTIES TIMEOUT 60)
