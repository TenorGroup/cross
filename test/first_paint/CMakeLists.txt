add_executable(FirstPaintTest FirstPaintTest.cpp)
target_include_directories(FirstPaintTest PRIVATE ${REPO_ROOT}/src)
target_link_libraries(FirstPaintTest PRIVATE GTest::gtest_main)
gtest_discover_tests(FirstPaintTest)
