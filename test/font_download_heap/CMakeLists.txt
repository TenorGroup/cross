find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME FontDownloadHeap
  COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/run.py"
          --repo "${REPO_ROOT}" --out "${CMAKE_CURRENT_BINARY_DIR}/heap")
set_tests_properties(FontDownloadHeap PROPERTIES TIMEOUT 120)
