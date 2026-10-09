find_package(Python3 COMPONENTS Interpreter REQUIRED)
foreach(case T1 T5)
  add_test(NAME BleHoldConnect${case}
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run.py"
      "${REPO_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}" "${case}" "${CMAKE_CXX_COMPILER}")
endforeach()
