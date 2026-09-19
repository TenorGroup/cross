find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_test(
  NAME BleLifecycleRuntimeIntegration
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run_runtime.py"
          --source "${REPO_ROOT}"
          --output "${CMAKE_CURRENT_BINARY_DIR}/runtime"
          --compiler "${CMAKE_CXX_COMPILER}"
)

add_test(
  NAME BleSleepTransitionIntegration
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/sleep/run.py"
          --source "${REPO_ROOT}"
          --output "${CMAKE_CURRENT_BINARY_DIR}/sleep"
          --compiler "${CMAKE_CXX_COMPILER}"
)

set_tests_properties(
  BleLifecycleRuntimeIntegration BleSleepTransitionIntegration
  PROPERTIES TIMEOUT 60 LABELS "ble;integration"
)
