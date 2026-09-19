find_package(Python3 COMPONENTS Interpreter REQUIRED)
get_filename_component(BLE_READER_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
add_test(
  NAME ble_reader_main_integration
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run_reader_tests.py"
          "${BLE_READER_SOURCE_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/production-projection"
          --compiler "${CMAKE_CXX_COMPILER}"
)
set_tests_properties(ble_reader_main_integration PROPERTIES TIMEOUT 60)
