add_executable(OtaLogTest OtaLogTest.cpp)
target_link_libraries(OtaLogTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(OtaLogTest)
