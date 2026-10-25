# Standalone capability and consumer allocation regression.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME SettingsRamRegression
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/run.py"
          --repo "${REPO_ROOT}"
          --work "${CMAKE_CURRENT_BINARY_DIR}/settings-ram-regression"
          --cmake "${CMAKE_COMMAND}" --jobs 2)
set_tests_properties(SettingsRamRegression PROPERTIES RUN_SERIAL TRUE TIMEOUT 300)

add_test(NAME SettingsCategoryRegression
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/category/run.py"
          --repo "${REPO_ROOT}"
          --output "${CMAKE_CURRENT_BINARY_DIR}/settings-category-regression"
          --sanitize)
set_tests_properties(SettingsCategoryRegression PROPERTIES RUN_SERIAL TRUE TIMEOUT 180)

add_test(NAME SettingsSaveFailureRegression
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/save_failure/run.py"
          --repo "${REPO_ROOT}"
          --output "${CMAKE_CURRENT_BINARY_DIR}/settings-save-failure-regression"
          --sanitize)
set_tests_properties(SettingsSaveFailureRegression PROPERTIES RUN_SERIAL TRUE TIMEOUT 180)

# Settings > Wi-Fi hands the time zone lookup to a fresh boot (the heap under Settings is too small for its TLS).
add_test(NAME SettingsWifiRowFreshBootClockSync
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/wifi_row/run.py" --repo "${REPO_ROOT}"
          --cxx "${CMAKE_CXX_COMPILER}")
