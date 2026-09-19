find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(
  NAME txt_zip_io_integrity
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run.py
    --source ${CMAKE_CURRENT_SOURCE_DIR}/../..
    --output ${CMAKE_CURRENT_BINARY_DIR}/regression
    --cxx ${CMAKE_CXX_COMPILER}
)
set_tests_properties(txt_zip_io_integrity PROPERTIES TIMEOUT 120)
