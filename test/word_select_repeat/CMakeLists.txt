# Auto-repeat ramp of the quote selector's Left/Right hold. Pure timing, no hardware.
add_executable(WordSelectRepeatTest
  WordSelectRepeatTest.cpp
  ${REPO_ROOT}/src/activities/reader/WordSelectRepeat.cpp
)
target_include_directories(WordSelectRepeatTest PRIVATE ${REPO_ROOT}/src)
target_link_libraries(WordSelectRepeatTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(WordSelectRepeatTest)
