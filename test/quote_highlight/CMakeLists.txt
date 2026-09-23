# Anchor arithmetic over a real Page, so the walk under test is the production walk.
add_executable(QuoteHighlightTest
  QuoteHighlightTest.cpp
  ${REPO_ROOT}/src/QuoteHighlight.cpp
  ${REPO_ROOT}/test/chapter_html_slim_parser/ParserLinkStubs.cpp
  ${REPO_ROOT}/lib/Epub/Epub/Page.cpp
  ${REPO_ROOT}/lib/Epub/Epub/blocks/TextBlock.cpp
  ${REPO_ROOT}/lib/MiniBidi/BidiUtils.cpp
  ${REPO_ROOT}/lib/MiniBidi/minibidi.c
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)
target_include_directories(QuoteHighlightTest PRIVATE
  ${REPO_ROOT}/test/chapter_html_slim_parser/stubs
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib ${REPO_ROOT}/lib/Epub ${REPO_ROOT}/lib/Epub/Epub
  ${REPO_ROOT}/lib/EpdFont ${REPO_ROOT}/lib/Memory ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/Utf8 ${REPO_ROOT}/lib/MiniBidi
  ${REPO_ROOT}/lib/FsHelpers ${REPO_ROOT}/lib/XmlParserUtils
)
target_link_libraries(QuoteHighlightTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(QuoteHighlightTest)

# The store keeps the anchor next to the quote text, in the file format already on the
# card. Reuses the filesystem-backed storage and JSON stubs of the history suite.
add_executable(QuoteAnchorStoreTest
  QuoteAnchorStoreTest.cpp
  QuoteNameStoreTest.cpp
  QuoteLatestTest.cpp
  ${REPO_ROOT}/src/QuoteStore.cpp
)
target_include_directories(QuoteAnchorStoreTest BEFORE PRIVATE
  ${REPO_ROOT}/test/history_navigation/stubs
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/src
  ${REPO_ROOT}/.pio/libdeps/gh_release/ArduinoJson/src
)
target_compile_definitions(QuoteAnchorStoreTest PRIVATE ARDUINOJSON_ENABLE_ARDUINO_STRING=0)
target_link_libraries(QuoteAnchorStoreTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(QuoteAnchorStoreTest)
