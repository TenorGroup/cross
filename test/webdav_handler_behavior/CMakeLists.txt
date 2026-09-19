find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME WebDAVHandlerProductionBehavior
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run.py
          --build ${CMAKE_CURRENT_BINARY_DIR}/production-methods)
