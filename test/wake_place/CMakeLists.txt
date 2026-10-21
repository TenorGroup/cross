add_executable(WakePlaceTest WakePlaceTest.cpp)
target_include_directories(WakePlaceTest PRIVATE ${REPO_ROOT}/src)
target_link_libraries(WakePlaceTest PRIVATE GTest::gtest_main)
gtest_discover_tests(WakePlaceTest)
