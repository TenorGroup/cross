if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.16)
  project(FileTransferLoopTests LANGUAGES CXX)
  enable_testing()
endif()

find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME FileTransferProductionLoop
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run.py
    --output ${CMAKE_CURRENT_BINARY_DIR}/harness --cxx ${CMAKE_CXX_COMPILER})
