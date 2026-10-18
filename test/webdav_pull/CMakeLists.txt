add_executable(WebDavPullTest WebDavPullTest.cpp ${REPO_ROOT}/src/network/WebDavPull.cpp)
target_link_libraries(WebDavPullTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(WebDavPullTest)
