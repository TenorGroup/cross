# Real GfxRenderer.cpp + InlineSymbols.cpp + ButtonSymbols.cpp on host, so the
# Back-arrow shape and its footer bounds run through the exact production
# pixel-drawing code (fillRect/fillPolygon/drawArc), not a re-implementation.
# Follows the sd_font_lifetime recipe: a display boundary backed by a RAM
# vector supplies the framebuffer GfxRenderer paints into.
add_executable(ButtonSymbolsBackShapeTest
  ButtonSymbolsBackShapeTest.cpp
  ${REPO_ROOT}/lib/GfxRenderer/GfxRenderer.cpp
  ${REPO_ROOT}/lib/GfxRenderer/FontCacheManager.cpp
  ${REPO_ROOT}/lib/GfxRenderer/InlineSymbols.cpp
  ${REPO_ROOT}/src/components/ButtonSymbols.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFont.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFontFamily.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${REPO_ROOT}/lib/Epub/Epub/blocks/TextBlock.cpp
  ${REPO_ROOT}/lib/MiniBidi/BidiUtils.cpp
  ${REPO_ROOT}/lib/MiniBidi/minibidi.c
)
target_compile_features(ButtonSymbolsBackShapeTest PRIVATE cxx_std_20)
target_include_directories(ButtonSymbolsBackShapeTest BEFORE PRIVATE
  stubs
)
target_include_directories(ButtonSymbolsBackShapeTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/src/components
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/GfxRenderer
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/MiniBidi
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/hal
  ${REPO_ROOT}/lib/I18n
  ${REPO_ROOT}/freeink-sdk/libs/display/FreeInkDisplay/include
)
# Section pruning leaves SD-card font and other unrelated GfxRenderer paths
# out of this host target, while compiling the real renderer/symbol sources
# unchanged.
target_compile_options(ButtonSymbolsBackShapeTest PRIVATE -ffunction-sections -fdata-sections)
if(APPLE)
  target_link_options(ButtonSymbolsBackShapeTest PRIVATE -Wl,-dead_strip)
else()
  target_link_options(ButtonSymbolsBackShapeTest PRIVATE -Wl,--gc-sections)
endif()
add_test(NAME ButtonSymbols.BackShape COMMAND ButtonSymbolsBackShapeTest)
