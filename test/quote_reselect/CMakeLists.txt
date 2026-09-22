# Mapping a saved quote back onto the words of its page, for reselecting it in the reader.
# Links the production anchor arithmetic of QuoteHighlight, so the rule under test is the
# same one the reader draws highlights with.
add_executable(QuoteReselectTest
  QuoteReselectTest.cpp
  ${REPO_ROOT}/src/QuoteHighlight.cpp
  ${REPO_ROOT}/test/chapter_html_slim_parser/ParserLinkStubs.cpp
  ${REPO_ROOT}/lib/Epub/Epub/Page.cpp
  ${REPO_ROOT}/lib/Epub/Epub/blocks/TextBlock.cpp
  ${REPO_ROOT}/lib/MiniBidi/BidiUtils.cpp
  ${REPO_ROOT}/lib/MiniBidi/minibidi.c
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)
target_include_directories(QuoteReselectTest PRIVATE
  ${REPO_ROOT}/test/chapter_html_slim_parser/stubs
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib ${REPO_ROOT}/lib/Epub ${REPO_ROOT}/lib/Epub/Epub
  ${REPO_ROOT}/lib/EpdFont ${REPO_ROOT}/lib/Memory ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/Utf8 ${REPO_ROOT}/lib/MiniBidi
  ${REPO_ROOT}/lib/FsHelpers ${REPO_ROOT}/lib/XmlParserUtils
)
target_link_libraries(QuoteReselectTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(QuoteReselectTest)
