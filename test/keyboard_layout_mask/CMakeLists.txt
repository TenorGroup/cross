if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.16)
  project(KeyboardLayoutMaskTests LANGUAGES CXX)
  enable_testing()
  get_filename_component(REPO_ROOT "../.." ABSOLUTE)
endif()

add_executable(KeyboardLayoutMaskTest
  KeyboardLayoutMaskTest.cpp
  ${REPO_ROOT}/src/activities/util/KeyboardLayoutSet.cpp
)
target_compile_features(KeyboardLayoutMaskTest PRIVATE cxx_std_17)
target_include_directories(KeyboardLayoutMaskTest PRIVATE
  stubs
  ${REPO_ROOT}/src
  ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/include
)
add_test(NAME KeyboardLayoutMask COMMAND KeyboardLayoutMaskTest)
