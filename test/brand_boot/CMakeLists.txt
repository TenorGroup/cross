add_test(NAME BrandBootProduction COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/run.py" --output "${CMAKE_CURRENT_BINARY_DIR}/production")
