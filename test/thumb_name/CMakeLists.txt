add_executable(ThumbNameTest ThumbNameTest.cpp)
target_include_directories(ThumbNameTest PRIVATE ${REPO_ROOT}/src)
target_link_libraries(ThumbNameTest PRIVATE GTest::gtest_main)
gtest_discover_tests(ThumbNameTest)
