add_executable(TruncatedTextAccumulatorTest
  TruncatedTextAccumulatorTest.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFont.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFontFamily.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

target_include_directories(TruncatedTextAccumulatorTest PRIVATE
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/Utf8
)

target_link_libraries(TruncatedTextAccumulatorTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(TruncatedTextAccumulatorTest)
