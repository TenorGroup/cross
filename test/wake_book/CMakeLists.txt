add_executable(WakeBookTest WakeBookTest.cpp)
target_include_directories(WakeBookTest PRIVATE ${REPO_ROOT}/src)
target_link_libraries(WakeBookTest PRIVATE GTest::gtest_main)
gtest_discover_tests(WakeBookTest)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME UglyStartBootGuard
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/start_screen_boot.py
          --repo ${REPO_ROOT} --output ${CMAKE_CURRENT_BINARY_DIR}/boot-guard)
