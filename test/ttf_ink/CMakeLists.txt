find_package(Freetype REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_executable(TtfInkTest
  TtfInkTest.cpp
  ${REPO_ROOT}/lib/EpdFont/TtfEpdFont.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFont.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFontFamily.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${REPO_ROOT}/freeink-sdk/libs/font/FreeInkFont/src/FtFont.cpp
  ${REPO_ROOT}/freeink-sdk/libs/font/FreeInkFont/src/Gpos.cpp
  ${REPO_ROOT}/freeink-sdk/libs/font/FreeInkFont/src/Gsub.cpp
  ${REPO_ROOT}/freeink-sdk/libs/font/FreeInkFont/src/FontAlloc.c
)
target_include_directories(TtfInkTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/src
  ${REPO_ROOT}/freeink-sdk/libs/font/FreeInkFont/include
  ${REPO_ROOT}/test/builtin_ink/stubs
)
target_compile_definitions(TtfInkTest PRIVATE BOARD_HAS_PSRAM=1 FREEINK_FONT_ENABLE_AUTOHINT=1
  FREEINK_FONT_ENABLE_NATIVE_HINTING=1 FREEINK_FONT_ENABLE_MONOCHROME=1 TEST_REPO_ROOT="${REPO_ROOT}")
target_link_libraries(TtfInkTest PRIVATE crosspoint_test_common GTest::gtest_main Freetype::Freetype)
gtest_discover_tests(TtfInkTest)
add_test(NAME TtfInkIdentityContract COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/identity.py")
