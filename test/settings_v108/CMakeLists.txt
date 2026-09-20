if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.16)
  project(SettingsV108Tests LANGUAGES CXX)
  enable_testing()
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME SettingsV108Callbacks
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run.py"
          --repo "${REPO_ROOT}" --output "${CMAKE_CURRENT_BINARY_DIR}/callback")
add_test(NAME SettingsV108WebCallbacks
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/web_run.py"
          --repo "${REPO_ROOT}" --output "${CMAKE_CURRENT_BINARY_DIR}/web-callback")
