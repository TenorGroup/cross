add_executable(TouchScrollTest TouchScrollTest.cpp)
target_include_directories(TouchScrollTest PRIVATE ${REPO_ROOT}/src)
target_link_libraries(TouchScrollTest PRIVATE GTest::gtest_main)
gtest_discover_tests(TouchScrollTest)
