add_executable(HttpUrlTest HttpUrlTest.cpp)
target_include_directories(HttpUrlTest PRIVATE ${REPO_ROOT}/freeink-sdk/libs/network/SecureNet/include)
target_link_libraries(HttpUrlTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(HttpUrlTest)
