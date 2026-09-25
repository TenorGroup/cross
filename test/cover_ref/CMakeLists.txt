add_executable(CoverRefTest CoverRefTest.cpp)
target_include_directories(CoverRefTest PRIVATE ${REPO_ROOT}/src)
target_link_libraries(CoverRefTest PRIVATE GTest::gtest_main)
gtest_discover_tests(CoverRefTest)
