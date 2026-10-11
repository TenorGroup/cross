add_test(NAME BitmapWriter.plain COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run.py
  --output ${CMAKE_CURRENT_BINARY_DIR}/plain --cxx ${CMAKE_CXX_COMPILER})
add_test(NAME BitmapWriter.dither COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run.py
  --output ${CMAKE_CURRENT_BINARY_DIR}/dither --cxx ${CMAKE_CXX_COMPILER} --dither)
add_test(NAME BitmapWriter.heap COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/heap.py
  --output ${CMAKE_CURRENT_BINARY_DIR}/heap --cxx ${CMAKE_CXX_COMPILER})
