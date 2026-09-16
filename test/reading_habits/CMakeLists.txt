add_executable(ReadingHabitsTest ReadingHabitsTest.cpp ${REPO_ROOT}/src/util/ReadingHabits.cpp ${REPO_ROOT}/src/util/NgayGio.cpp)
target_include_directories(ReadingHabitsTest PRIVATE ${REPO_ROOT}/src)
target_link_libraries(ReadingHabitsTest PRIVATE GTest::gtest_main)
gtest_discover_tests(ReadingHabitsTest)
