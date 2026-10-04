# Which screens the X4 Pro draws: tenor/cross everywhere, no tip that names a front button.
# Built twice from one source, as the X4 Pro and as a button reader.
foreach(_board X4Pro Buttons)
  add_executable(TouchShell${_board}Test TouchShellTest.cpp)
  target_include_directories(TouchShell${_board}Test BEFORE PRIVATE ${REPO_ROOT}/test/button_symbols/stubs)
  target_include_directories(TouchShell${_board}Test PRIVATE ${REPO_ROOT}/src ${REPO_ROOT}/src/components)
  target_link_libraries(TouchShell${_board}Test PRIVATE GTest::gtest_main)
  gtest_discover_tests(TouchShell${_board}Test)
endforeach()
target_compile_definitions(TouchShellX4ProTest PRIVATE FREEINK_DEVICE_X4PRO=1)
