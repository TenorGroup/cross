find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME KoreaderWireMatchesV1076 COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run.py)
