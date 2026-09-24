# Books with thousands of chapters under a device-sized heap cap.
add_executable(HugeBookIndexTest
  HugeBookIndexTest.cpp
  ${REPO_ROOT}/lib/Epub/Epub/BookMetadataCache.cpp
  ${REPO_ROOT}/lib/Epub/Epub/parsers/ContentOpfParser.cpp
  ${REPO_ROOT}/lib/Epub/Epub/parsers/TocNavParser.cpp
)
target_include_directories(HugeBookIndexTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Epub/Epub
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/XmlParserUtils
  ${REPO_ROOT}/test/host_stubs
)
target_link_libraries(HugeBookIndexTest PRIVATE GTest::gtest_main expat)
gtest_discover_tests(HugeBookIndexTest PROPERTIES RUN_SERIAL TRUE TIMEOUT 300)

add_executable(HugeBookSectionTest
  HugeBookSectionTest.cpp ../section_cache/ParserLinkStubs.cpp
  ${REPO_ROOT}/lib/Epub/Epub/Section.cpp
  ${REPO_ROOT}/lib/Epub/Epub/Page.cpp
  ${REPO_ROOT}/lib/Epub/Epub/blocks/TextBlock.cpp
  ${REPO_ROOT}/lib/MiniBidi/BidiUtils.cpp
  ${REPO_ROOT}/lib/MiniBidi/minibidi.c
  ${REPO_ROOT}/lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp
  ${REPO_ROOT}/lib/Epub/Epub/css/CssParser.cpp
  ${REPO_ROOT}/lib/Epub/Epub/ParsedText.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/ImageDimsProbe.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)
target_include_directories(HugeBookSectionTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/section_stubs
  ${CMAKE_CURRENT_SOURCE_DIR}
  ../section_cache/stubs
  ${REPO_ROOT}/lib ${REPO_ROOT}/lib/Epub ${REPO_ROOT}/lib/Epub/Epub
  ${REPO_ROOT}/lib/Epub/Epub/parsers ${REPO_ROOT}/lib/Epub/Epub/css
  ${REPO_ROOT}/lib/EpdFont ${REPO_ROOT}/lib/Utf8 ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/Serialization ${REPO_ROOT}/lib/MiniBidi ${REPO_ROOT}/lib/FsHelpers
  ${REPO_ROOT}/lib/XmlParserUtils
  ${REPO_ROOT}/freeink-sdk/libs/hardware/SDCardManager/include)
target_link_libraries(HugeBookSectionTest PRIVATE GTest::gtest_main expat)
gtest_discover_tests(HugeBookSectionTest PROPERTIES RUN_SERIAL TRUE TIMEOUT 300)
