add_test(NAME HomeThumbPower COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run.py --output ${CMAKE_CURRENT_BINARY_DIR}/power --case power)
add_test(NAME HomeThumbQueue COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run.py --output ${CMAKE_CURRENT_BINARY_DIR}/queue --case queue)
