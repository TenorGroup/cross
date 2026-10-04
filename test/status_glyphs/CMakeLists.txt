add_executable(StatusGlyphsTest StatusGlyphsTest.cpp)
target_include_directories(StatusGlyphsTest PRIVATE ${REPO_ROOT}/src)
target_link_libraries(StatusGlyphsTest PRIVATE GTest::gtest_main)
gtest_discover_tests(StatusGlyphsTest)
