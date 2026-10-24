add_executable(TxtPositionTest TxtPositionTest.cpp)

target_include_directories(TxtPositionTest PRIVATE
  ${REPO_ROOT}/src/activities/reader
)

target_link_libraries(TxtPositionTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(TxtPositionTest)
