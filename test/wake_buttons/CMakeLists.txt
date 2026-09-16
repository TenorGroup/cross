add_executable(WakeButtonsTest WakeButtonsTest.cpp)
target_include_directories(WakeButtonsTest PRIVATE ${REPO_ROOT}/lib/hal)
target_link_libraries(WakeButtonsTest PRIVATE GTest::gtest_main)
gtest_discover_tests(WakeButtonsTest)
