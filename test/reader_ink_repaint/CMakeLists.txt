find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME ReaderInkRepaintContract COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_repaint.py")
