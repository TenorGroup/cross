add_executable(x3_navigation_test X3NavigationTest.cpp)
target_link_libraries(x3_navigation_test PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(x3_navigation_test)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME X3Navigation.SinglePainter
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/check.py")
add_test(NAME X3Navigation.ActivityStack
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/stack.py"
          --work "${CMAKE_CURRENT_BINARY_DIR}/stack")
