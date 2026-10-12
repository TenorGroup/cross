if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.16)
  project(TelexComposerTests LANGUAGES CXX)
  enable_testing()
  get_filename_component(REPO_ROOT "../.." ABSOLUTE)
endif()

add_executable(TelexComposerTest
  TelexComposerTest.cpp
  ${REPO_ROOT}/src/activities/util/TelexComposer.cpp
)
target_compile_features(TelexComposerTest PRIVATE cxx_std_17)
target_include_directories(TelexComposerTest PRIVATE ${REPO_ROOT}/src)
target_compile_definitions(TelexComposerTest PRIVATE FREEINK_DEVICE_X4PRO=1)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME TelexComposer.corpus COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run.py" $<TARGET_FILE:TelexComposerTest>)
add_test(NAME TelexComposer.unit COMMAND TelexComposerTest)
