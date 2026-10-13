find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME FileBrowserStateProductionPaths
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run.py --output ${CMAKE_CURRENT_BINARY_DIR}/harness)
