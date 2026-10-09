add_executable(BuiltinInkTest
  BuiltinInkTest.cpp
  ${REPO_ROOT}/lib/EpdFont/FontDecompressor.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFont.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFontFamily.cpp
  ${REPO_ROOT}/lib/GfxRenderer/FontCacheManager.cpp
  ${REPO_ROOT}/lib/InflateReader/InflateReader.cpp
  ${REPO_ROOT}/lib/uzlib/src/tinflate.c
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${REPO_ROOT}/freeink-sdk/libs/font/FreeInkFont/src/FontAlloc.c
)
target_include_directories(BuiltinInkTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/GfxRenderer
  ${REPO_ROOT}/lib/InflateReader
  ${REPO_ROOT}/lib/uzlib/src
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/freeink-sdk/libs/font/FreeInkFont/include
)
find_package(ZLIB REQUIRED)
target_link_libraries(BuiltinInkTest PRIVATE crosspoint_test_common GTest::gtest_main ZLIB::ZLIB)
gtest_discover_tests(BuiltinInkTest)
