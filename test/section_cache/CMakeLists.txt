cmake_minimum_required(VERSION 3.16)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(section_cache_tests C CXX)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  enable_testing()
  include(GoogleTest)
  if(NOT REPO_ROOT)
    get_filename_component(REPO_ROOT "../.." ABSOLUTE)
  endif()
  if(NOT GTEST_SOURCE)
    message(FATAL_ERROR "Set GTEST_SOURCE to a local googletest source checkout")
  endif()
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
  add_subdirectory("${GTEST_SOURCE}" googletest EXCLUDE_FROM_ALL)
endif()
if(NOT SECTION_SOURCE_ROOT)
  set(SECTION_SOURCE_ROOT "${REPO_ROOT}")
endif()
add_executable(SectionCacheTest
  SectionCacheTest.cpp ParserLinkStubs.cpp
  ${SECTION_SOURCE_ROOT}/lib/Epub/Epub/Section.cpp
  ${SECTION_SOURCE_ROOT}/lib/Epub/Epub/Page.cpp
  ${SECTION_SOURCE_ROOT}/lib/Epub/Epub/blocks/TextBlock.cpp
  ${SECTION_SOURCE_ROOT}/lib/MiniBidi/BidiUtils.cpp
  ${SECTION_SOURCE_ROOT}/lib/MiniBidi/minibidi.c
  ${SECTION_SOURCE_ROOT}/lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp
  ${SECTION_SOURCE_ROOT}/lib/Epub/Epub/css/CssParser.cpp
  ${SECTION_SOURCE_ROOT}/lib/Epub/Epub/ParsedText.cpp
  ${SECTION_SOURCE_ROOT}/lib/Epub/Epub/converters/ImageDimsProbe.cpp
  ${SECTION_SOURCE_ROOT}/lib/Utf8/Utf8.cpp
)
target_include_directories(SectionCacheTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${SECTION_SOURCE_ROOT}/lib
  ${SECTION_SOURCE_ROOT}/lib/Epub
  ${SECTION_SOURCE_ROOT}/lib/Epub/Epub
  ${SECTION_SOURCE_ROOT}/lib/Epub/Epub/parsers
  ${SECTION_SOURCE_ROOT}/lib/Epub/Epub/css
  ${SECTION_SOURCE_ROOT}/lib/EpdFont
  ${SECTION_SOURCE_ROOT}/lib/Utf8
  ${SECTION_SOURCE_ROOT}/lib/Memory
  ${SECTION_SOURCE_ROOT}/lib/Serialization
  ${SECTION_SOURCE_ROOT}/lib/MiniBidi
  ${SECTION_SOURCE_ROOT}/lib/FsHelpers
  ${SECTION_SOURCE_ROOT}/lib/XmlParserUtils
  ${REPO_ROOT}/freeink-sdk/libs/hardware/SDCardManager/include)
target_link_libraries(SectionCacheTest PRIVATE GTest::gtest_main expat)
# Keep host scheduling contention out of the paired partial/cold timing gate.
gtest_discover_tests(SectionCacheTest PROPERTIES RUN_SERIAL TRUE)
