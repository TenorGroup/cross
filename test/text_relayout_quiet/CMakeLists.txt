add_executable(TextRelayoutQuietTest
  TextRelayoutQuietTest.cpp
)

target_link_libraries(TextRelayoutQuietTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(TextRelayoutQuietTest)
