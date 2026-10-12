add_test(NAME BrandBootProduction COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/run.py" --output "${CMAKE_CURRENT_BINARY_DIR}/production")
add_test(NAME BrandBootX3Art COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/x3_art.py")
add_test(NAME BrandBootX4ProArt COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/x4pro_art.py")
