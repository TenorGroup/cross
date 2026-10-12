# German hyphenation can be left out of a build with TENOR_HYPH_NO_DE (the S3 Bluetooth beta).
# HyphNoDeTest is built with the flag; HyphWithDeTest is the same test without it and proves
# the unflagged registry still carries German.
foreach(variant IN ITEMS NoDe WithDe)
  set(target Hyph${variant}Test)
  add_executable(${target}
    HyphNoDeTest.cpp
    ${REPO_ROOT}/lib/Epub/Epub/ParsedText.cpp
    ${REPO_ROOT}/lib/Epub/Epub/WordStore.cpp
    ${REPO_ROOT}/lib/Epub/Epub/blocks/TextBlock.cpp
    ${REPO_ROOT}/lib/Epub/Epub/hyphenation/Hyphenator.cpp
    ${REPO_ROOT}/lib/Epub/Epub/hyphenation/LanguageRegistry.cpp
    ${REPO_ROOT}/lib/Epub/Epub/hyphenation/LiangHyphenation.cpp
    ${REPO_ROOT}/lib/Epub/Epub/hyphenation/HyphenationCommon.cpp
    ${REPO_ROOT}/lib/MiniBidi/BidiUtils.cpp
    ${REPO_ROOT}/lib/MiniBidi/minibidi.c
    ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  )
  target_include_directories(${target} PRIVATE
    ${REPO_ROOT}/test/chapter_html_slim_parser/stubs
    ${REPO_ROOT}/lib
    ${REPO_ROOT}/lib/Epub
    ${REPO_ROOT}/lib/Epub/Epub
    ${REPO_ROOT}/lib/EpdFont
    ${REPO_ROOT}/lib/Utf8
    ${REPO_ROOT}/lib/Memory
    ${REPO_ROOT}/lib/Serialization
    ${REPO_ROOT}/lib/MiniBidi
    ${REPO_ROOT}/lib/FsHelpers
  )
  if(variant STREQUAL "NoDe")
    target_compile_definitions(${target} PRIVATE TENOR_HYPH_NO_DE=1)
  endif()
  target_link_libraries(${target} PRIVATE crosspoint_test_common GTest::gtest_main)
  gtest_discover_tests(${target} TEST_PREFIX ${variant}.)
endforeach()
