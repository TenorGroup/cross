cmake_minimum_required(VERSION 3.16)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(shed_parser_tests C CXX)
  enable_testing()
  set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
endif()
# Sources a red run swaps for a mutated copy (see run_red.sh); the checkout is never edited.
set(SHED_PARSED_TEXT_SOURCE "${REPO_ROOT}/lib/Epub/Epub/ParsedText.cpp" CACHE FILEPATH "ParsedText under test")
set(SHED_GFX_SOURCE "${REPO_ROOT}/lib/GfxRenderer/GfxRenderer.cpp" CACHE FILEPATH "GfxRenderer under test")
add_executable(ShedParserTest
  ShedParserTest.cpp
  ../section_cache/ParserLinkStubs.cpp
  ${REPO_ROOT}/lib/Epub/Epub/Section.cpp
  ${REPO_ROOT}/lib/Epub/Epub/Page.cpp
  ${REPO_ROOT}/lib/Epub/Epub/blocks/TextBlock.cpp
  ${REPO_ROOT}/lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp
  ${REPO_ROOT}/lib/Epub/Epub/css/CssParser.cpp
  ${SHED_PARSED_TEXT_SOURCE}
  ${REPO_ROOT}/lib/Epub/Epub/WordStore.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/ImageDimsProbe.cpp
  ${REPO_ROOT}/lib/EpdFont/SdCardFont.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFont.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFontFamily.cpp
  ${SHED_GFX_SOURCE}
  ${REPO_ROOT}/lib/GfxRenderer/FontCacheManager.cpp
  ${REPO_ROOT}/lib/GfxRenderer/InlineSymbols.cpp
  ${REPO_ROOT}/lib/MiniBidi/BidiUtils.cpp
  ${REPO_ROOT}/lib/MiniBidi/minibidi.c
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)
target_compile_features(ShedParserTest PRIVATE cxx_std_20)
# Own stubs first, then the font suite's, then the real renderer headers, then the section
# suite's (its Epub.h must win over lib/Epub/Epub.h, its GfxRenderer.h must lose to the real one).
target_include_directories(ShedParserTest PRIVATE
  stubs
  ../sd_font_lifetime/stubs
  ${REPO_ROOT}/lib/GfxRenderer
  ../section_cache/stubs
  ${REPO_ROOT}/lib
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Epub/Epub
  ${REPO_ROOT}/lib/Epub/Epub/parsers
  ${REPO_ROOT}/lib/Epub/Epub/css
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/MiniBidi
  ${REPO_ROOT}/lib/FsHelpers
  ${REPO_ROOT}/lib/XmlParserUtils
  ${REPO_ROOT}/lib/hal
  ${REPO_ROOT}/freeink-sdk/libs/hardware/SDCardManager/include
  ${REPO_ROOT}/freeink-sdk/libs/display/FreeInkDisplay/include
)
target_link_libraries(ShedParserTest PRIVATE expat)
target_compile_options(ShedParserTest PRIVATE -ffunction-sections -fdata-sections)
if(APPLE)
  target_link_options(ShedParserTest PRIVATE -Wl,-dead_strip)
else()
  target_link_options(ShedParserTest PRIVATE -Wl,--gc-sections)
endif()
option(SHED_ASAN "Run the shed/parser test with AddressSanitizer" OFF)
if(SHED_ASAN)
  target_compile_options(ShedParserTest PRIVATE -fsanitize=address -fno-omit-frame-pointer -g)
  target_link_options(ShedParserTest PRIVATE -fsanitize=address)
endif()
foreach(scenario premise determinism every-tick starve-sweep starve-every)
  add_test(NAME ShedParser.${scenario} COMMAND ShedParserTest ${scenario})
endforeach()
set_tests_properties(ShedParser.starve-sweep PROPERTIES TIMEOUT 900)
