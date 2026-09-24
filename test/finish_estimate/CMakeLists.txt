add_executable(FinishEstimateTest FinishEstimateTest.cpp ${REPO_ROOT}/src/util/ReadingHabits.cpp ${REPO_ROOT}/src/util/NgayGio.cpp)
target_include_directories(FinishEstimateTest PRIVATE ${REPO_ROOT}/src)
target_link_libraries(FinishEstimateTest PRIVATE GTest::gtest_main)
gtest_discover_tests(FinishEstimateTest)
