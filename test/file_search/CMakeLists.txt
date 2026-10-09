find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_test(
  NAME FileSearchProduction
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run.py"
          --source "${REPO_ROOT}/src/activities/home/FileBrowserActivity.cpp"
          --repo "${REPO_ROOT}"
          --output "${CMAKE_CURRENT_BINARY_DIR}"
)
