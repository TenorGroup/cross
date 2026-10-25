# The sleep screen of a JPEG: the .pxc the converter's PixelCache writes, made into the 2-bit BMP.
add_executable(SleepCoverPxcTest
  SleepCoverPxcTest.cpp
  ${REPO_ROOT}/lib/GfxRenderer/BitmapHelpers.cpp
)
target_include_directories(SleepCoverPxcTest BEFORE PRIVATE stubs)
target_include_directories(SleepCoverPxcTest PRIVATE
  ${REPO_ROOT}/src/activities/util
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/GfxRenderer
  ${REPO_ROOT}/lib/Memory
)
target_link_libraries(SleepCoverPxcTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(SleepCoverPxcTest)
