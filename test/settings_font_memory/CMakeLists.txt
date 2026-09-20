find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME SettingsFontMemoryRegression
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run.py"
          --repo "${REPO_ROOT}"
          --output "${CMAKE_CURRENT_BINARY_DIR}/settings-font-memory-regression"
          --sanitize)
set_tests_properties(SettingsFontMemoryRegression PROPERTIES RUN_SERIAL TRUE TIMEOUT 180)
