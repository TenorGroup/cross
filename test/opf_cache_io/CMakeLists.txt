add_executable(OpfCacheIoTest
  OpfCacheIoTest.cpp
  ${REPO_ROOT}/lib/Epub/Epub/parsers/ContentOpfParser.cpp
)
target_include_directories(OpfCacheIoTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/test/chapter_html_slim_parser/stubs
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/XmlParserUtils
)
target_link_libraries(OpfCacheIoTest PRIVATE crosspoint_test_common GTest::gtest_main expat)
gtest_discover_tests(OpfCacheIoTest)
