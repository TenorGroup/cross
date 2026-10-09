cmake_minimum_required(VERSION 3.16)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(sd_font_lifetime_tests C CXX)
  enable_testing()
  set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
endif()
set(SD_FONT_SOURCE "${REPO_ROOT}/lib/EpdFont/SdCardFont.cpp" CACHE FILEPATH "SdCardFont implementation under test")
add_executable(SdFontLifetimeTest
  SdFontLifetimeTest.cpp
  ${SD_FONT_SOURCE}
  ${REPO_ROOT}/lib/EpdFont/EpdFont.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFontFamily.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${REPO_ROOT}/lib/GfxRenderer/GfxRenderer.cpp
  ${REPO_ROOT}/lib/GfxRenderer/FontCacheManager.cpp
  ${REPO_ROOT}/lib/GfxRenderer/InlineSymbols.cpp
  ${REPO_ROOT}/lib/Epub/Epub/blocks/TextBlock.cpp
  ${REPO_ROOT}/lib/MiniBidi/BidiUtils.cpp
  ${REPO_ROOT}/lib/MiniBidi/minibidi.c
)
target_compile_features(SdFontLifetimeTest PRIVATE cxx_std_20)
target_include_directories(SdFontLifetimeTest PRIVATE
  stubs
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/GfxRenderer
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/MiniBidi
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/hal
  ${REPO_ROOT}/freeink-sdk/libs/display/FreeInkDisplay/include
)
# Section pruning leaves unrelated bitmap I/O and panel operations out of this
# host target, while compiling full renderer/font/block source files unchanged.
target_compile_options(SdFontLifetimeTest PRIVATE -ffunction-sections -fdata-sections)
if(APPLE)
  target_link_options(SdFontLifetimeTest PRIVATE -Wl,-dead_strip)
else()
  target_link_options(SdFontLifetimeTest PRIVATE -Wl,--gc-sections)
endif()
option(SD_FONT_ASAN "Run font lifetime regression with AddressSanitizer" OFF)
if(SD_FONT_ASAN)
  target_compile_options(SdFontLifetimeTest PRIVATE -fsanitize=address -fno-omit-frame-pointer -g)
  target_link_options(SdFontLifetimeTest PRIVATE -fsanitize=address)
endif()
foreach(scenario ruby-base ruby-annotation repeat-release failed-read failed-seek failed-reload
                 advance-low-heap advance-cjk advance-cap advance-growth-failure advance-growth-peak)
  add_test(NAME SdFontLifetime.${scenario} COMMAND SdFontLifetimeTest ${scenario})
endforeach()

get_target_property(NOTO_SOURCES SdFontLifetimeTest SOURCES)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
list(REMOVE_ITEM NOTO_SOURCES SdFontLifetimeTest.cpp)
add_executable(NotoBuiltinRasterTest NotoBuiltinRasterTest.cpp ${NOTO_SOURCES})
get_target_property(NOTO_INCLUDES SdFontLifetimeTest INCLUDE_DIRECTORIES)
target_include_directories(NotoBuiltinRasterTest PRIVATE ${NOTO_INCLUDES})
target_compile_features(NotoBuiltinRasterTest PRIVATE cxx_std_20)
target_compile_options(NotoBuiltinRasterTest PRIVATE -ffunction-sections -fdata-sections)
if(APPLE)
  target_link_options(NotoBuiltinRasterTest PRIVATE -Wl,-dead_strip)
else()
  target_link_options(NotoBuiltinRasterTest PRIVATE -Wl,--gc-sections)
endif()
add_test(NAME NotoBuiltinRaster COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/test_noto.py" "$<TARGET_FILE:NotoBuiltinRasterTest>")
set_tests_properties(NotoBuiltinRaster PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME NotoBuiltinSelection COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/test_noto_selection.py")
