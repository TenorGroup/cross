add_executable(TtfRampTest
  TtfRampTest.cpp
  ${REPO_ROOT}/lib/GfxRenderer/FontCacheManager.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

# stubs/ holds only the vector font stand-in and must come before the shared stubs, whose TtfEpdFont.h turns the path off.
target_include_directories(TtfRampTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/test/font_cache_manager/stubs
  ${REPO_ROOT}/lib/GfxRenderer
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/src
)

target_link_libraries(TtfRampTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(TtfRampTest)
