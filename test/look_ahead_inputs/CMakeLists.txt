find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME look_ahead_real_input_routes
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run_tests.py"
          "${REPO_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/production-projection"
          --compiler "${CMAKE_CXX_COMPILER}")
set_tests_properties(look_ahead_real_input_routes PROPERTIES TIMEOUT 90)
