find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME UglyMarks
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/check.py
    --output ${CMAKE_CURRENT_BINARY_DIR}/marks)
