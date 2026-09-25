add_executable(WakeBookTest WakeBookTest.cpp)
target_include_directories(WakeBookTest PRIVATE ${REPO_ROOT}/src)
target_link_libraries(WakeBookTest PRIVATE GTest::gtest_main)
gtest_discover_tests(WakeBookTest)
