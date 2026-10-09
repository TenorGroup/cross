# Which screens the X4 Pro draws: tenor/cross everywhere, no tip that names a front button.
# Built twice from one source, as the X4 Pro and as a button reader.
set(_chrome_file "${REPO_ROOT}/src/components/TenorMenuChrome.cpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_chrome_file}")
file(READ "${_chrome_file}" _chrome_source)
string(REGEX MATCH "bool tenorchrome::x3ScreenLayoutEnabled\\(\\)[ \t\r\n]*\\{[^}]*\\}" _layout_policy "${_chrome_source}")
if(NOT _layout_policy)
  message(FATAL_ERROR "X3 screen layout policy missing")
endif()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/X3ScreenLayoutPolicy.inc" "${_layout_policy}\n")
foreach(_board X4Pro Buttons)
  add_executable(TouchShell${_board}Test TouchShellTest.cpp)
  target_include_directories(TouchShell${_board}Test BEFORE PRIVATE ${REPO_ROOT}/test/button_symbols/stubs)
  target_include_directories(TouchShell${_board}Test PRIVATE ${REPO_ROOT}/src ${REPO_ROOT}/src/components ${CMAKE_CURRENT_BINARY_DIR})
  target_link_libraries(TouchShell${_board}Test PRIVATE GTest::gtest_main)
  gtest_discover_tests(TouchShell${_board}Test)
endforeach()
target_compile_definitions(TouchShellX4ProTest PRIVATE FREEINK_DEVICE_X4PRO=1)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME TouchShell.FixedDynamicBar
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/dynamic_bar.py"
          --work "${CMAKE_CURRENT_BINARY_DIR}/dynamic-bar")
