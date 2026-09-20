if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.16)
  project(KeyboardSyncTests LANGUAGES CXX)
  enable_testing()
  get_filename_component(REPO_ROOT "../.." ABSOLUTE)
endif()

find_package(Threads REQUIRED)
add_executable(KeyboardSyncTest
  KeyboardSyncTest.cpp
  ${REPO_ROOT}/src/activities/util/KeyboardEntryActivity.cpp
  ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/src/FreeInkUI.cpp
)
target_compile_features(KeyboardSyncTest PRIVATE cxx_std_17)
target_include_directories(KeyboardSyncTest PRIVATE
  stubs
  ${REPO_ROOT}/src
  ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/include
)
target_link_libraries(KeyboardSyncTest PRIVATE Threads::Threads)
foreach(case enter insert backspace clear cursor password password_utf8 utf8_wrap input_time_short input_time_long touch completion cancel viewport timeout_disabled timeout_wrap timeout_input_reset timeout_normal_cancel stress)
  add_test(NAME KeyboardSync.${case} COMMAND KeyboardSyncTest ${case})
endforeach()
