find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME JumpBarClock
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/clock.py"
    --repo "${REPO_ROOT}" --output "${CMAKE_CURRENT_BINARY_DIR}")
