find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_test(NAME TransferEntryProductionContract
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run.py
    --output ${CMAKE_CURRENT_BINARY_DIR}/harness --cxx ${CMAKE_CXX_COMPILER})
