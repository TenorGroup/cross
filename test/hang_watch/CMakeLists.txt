add_executable(HangWatchTest HangWatchTest.cpp)
target_include_directories(HangWatchTest PRIVATE ${REPO_ROOT}/src)
target_link_libraries(HangWatchTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(HangWatchTest)
