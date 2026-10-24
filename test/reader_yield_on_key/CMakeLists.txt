find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME reader_yield_on_key
  COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_CURRENT_SOURCE_DIR}/check.py" "${REPO_ROOT}")
