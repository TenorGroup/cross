if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.16)
  project(BootTrialTests LANGUAGES CXX)
  enable_testing()
endif()
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(BOOT_TRIAL_MAIN_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../../src/main.cpp" CACHE FILEPATH
  "Startup adapter source for isolated integration checks")
add_test(NAME BootTrialProduction
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run.py
    --output ${CMAKE_CURRENT_BINARY_DIR}/harness --cxx ${CMAKE_CXX_COMPILER}
    --main-source ${BOOT_TRIAL_MAIN_SOURCE})
