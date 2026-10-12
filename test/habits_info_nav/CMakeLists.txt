find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME HabitsInfoNav
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run.py"
    --compiler "${CMAKE_CXX_COMPILER}"
    --output "${CMAKE_CURRENT_BINARY_DIR}/evidence")
