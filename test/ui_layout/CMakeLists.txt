if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.16)
  project(UiLayoutTests LANGUAGES CXX)
  enable_testing()
  get_filename_component(REPO_ROOT "../.." ABSOLUTE)
endif()
set(UI_LAYOUT_SDK "${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI")
foreach(name geometry option_window stats_layout stats_navigation list_render keyboard_render option_render list_follow)
  add_executable(UiLayout_${name} ${name}.cpp)
  target_compile_features(UiLayout_${name} PRIVATE cxx_std_20)
  target_compile_options(UiLayout_${name} PRIVATE -UNDEBUG)
  target_include_directories(UiLayout_${name} PRIVATE "${REPO_ROOT}/src" "${UI_LAYOUT_SDK}/include")
  if(name MATCHES "_render$" OR name STREQUAL "list_follow")
    target_sources(UiLayout_${name} PRIVATE "${UI_LAYOUT_SDK}/src/FreeInkUI.cpp")
  endif()
  add_test(NAME UiLayout.${name} COMMAND UiLayout_${name})
endforeach()
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME UiLayout.footer_tips COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_footer_tips.py")
add_test(NAME UiLayout.sibling_arrows COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_sibling_arrows.py")
add_test(NAME UiLayout.more_chevron COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_more_chevron.py")
add_test(NAME UiLayout.icon_symmetry COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_icon_symmetry.py")
add_test(NAME UiLayout.clock_guard COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_clock_guard.py")
add_test(NAME UiLayout.list_render_wiring COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_list_render_wiring.py")
add_executable(UiLayout_stats_render stats_render.cpp
  "${REPO_ROOT}/src/components/ReadingStatsView.cpp"
  "${REPO_ROOT}/src/components/ReadingStatsFormat.cpp"
  "${REPO_ROOT}/src/util/NgayGio.cpp")
target_compile_features(UiLayout_stats_render PRIVATE cxx_std_20)
target_compile_options(UiLayout_stats_render PRIVATE -UNDEBUG)
target_include_directories(UiLayout_stats_render PRIVATE stats_stubs "${REPO_ROOT}/src")
add_test(NAME UiLayout.stats_render COMMAND UiLayout_stats_render)
add_executable(UiLayout_quote_list quote_list.cpp "${REPO_ROOT}/src/components/QuoteListLayout.cpp")
target_compile_features(UiLayout_quote_list PRIVATE cxx_std_20)
target_compile_options(UiLayout_quote_list PRIVATE -UNDEBUG)
target_include_directories(UiLayout_quote_list PRIVATE "${REPO_ROOT}/src")
add_test(NAME UiLayout.quote_list COMMAND UiLayout_quote_list)
add_executable(UiLayout_quote_detail quote_detail.cpp "${REPO_ROOT}/src/components/QuoteDetailLayout.cpp")
target_compile_features(UiLayout_quote_detail PRIVATE cxx_std_20)
target_compile_options(UiLayout_quote_detail PRIVATE -UNDEBUG)
target_include_directories(UiLayout_quote_detail PRIVATE "${REPO_ROOT}/src")
add_test(NAME UiLayout.quote_detail COMMAND UiLayout_quote_detail)
add_executable(UiLayout_home_excerpt home_excerpt.cpp "${REPO_ROOT}/lib/Utf8/Utf8.cpp"
  "${REPO_ROOT}/lib/EpdFont/EpdFont.cpp")
target_compile_features(UiLayout_home_excerpt PRIVATE cxx_std_20)
target_compile_options(UiLayout_home_excerpt PRIVATE -UNDEBUG)
target_include_directories(UiLayout_home_excerpt PRIVATE "${REPO_ROOT}/src" "${REPO_ROOT}/lib/Utf8" "${REPO_ROOT}/lib/EpdFont")
add_test(NAME UiLayout.home_excerpt COMMAND UiLayout_home_excerpt)
add_executable(UiLayout_compact_stats compact_stats.cpp)
target_compile_features(UiLayout_compact_stats PRIVATE cxx_std_20)
target_compile_options(UiLayout_compact_stats PRIVATE -UNDEBUG)
target_include_directories(UiLayout_compact_stats PRIVATE "${REPO_ROOT}/src")
add_test(NAME UiLayout.compact_stats COMMAND UiLayout_compact_stats)
add_executable(UiLayout_tenor_radius tenor_radius.cpp)
target_compile_features(UiLayout_tenor_radius PRIVATE cxx_std_20)
target_compile_options(UiLayout_tenor_radius PRIVATE -UNDEBUG)
target_include_directories(UiLayout_tenor_radius PRIVATE "${REPO_ROOT}/src" "${REPO_ROOT}/lib/GfxRenderer")
add_test(NAME UiLayout.tenor_radius COMMAND UiLayout_tenor_radius)
add_executable(UiLayout_corner_cost corner_cost.cpp)
target_compile_features(UiLayout_corner_cost PRIVATE cxx_std_20)
target_compile_options(UiLayout_corner_cost PRIVATE -UNDEBUG)
target_include_directories(UiLayout_corner_cost PRIVATE "${REPO_ROOT}/lib/GfxRenderer")
add_test(NAME UiLayout.corner_cost COMMAND UiLayout_corner_cost)
# Rounded blocks through the real GfxRenderer.cpp, with the button_symbols test's stubs and a RAM
# framebuffer, so the corner shape is checked on the pixels the panel gets. The sleep dither rides
# on the same recipe.
foreach(name rounded_render rounded_border sleep_gray_planes bitmap_rows)
add_executable(UiLayout_${name} ${name}.cpp
  "${REPO_ROOT}/lib/GfxRenderer/GfxRenderer.cpp"
  "${REPO_ROOT}/lib/GfxRenderer/FontCacheManager.cpp"
  "${REPO_ROOT}/lib/GfxRenderer/InlineSymbols.cpp"
  "${REPO_ROOT}/lib/EpdFont/EpdFont.cpp"
  "${REPO_ROOT}/lib/EpdFont/EpdFontFamily.cpp"
  "${REPO_ROOT}/lib/Utf8/Utf8.cpp"
  "${REPO_ROOT}/lib/Epub/Epub/blocks/TextBlock.cpp"
  "${REPO_ROOT}/lib/MiniBidi/BidiUtils.cpp"
  "${REPO_ROOT}/lib/MiniBidi/minibidi.c")
target_compile_features(UiLayout_${name} PRIVATE cxx_std_20)
target_include_directories(UiLayout_${name} BEFORE PRIVATE "${REPO_ROOT}/test/button_symbols/stubs")
target_include_directories(UiLayout_${name} PRIVATE "${REPO_ROOT}/src" "${REPO_ROOT}/src/components"
  "${REPO_ROOT}/lib/EpdFont" "${REPO_ROOT}/lib/Utf8" "${REPO_ROOT}/lib/GfxRenderer" "${REPO_ROOT}/lib/Epub"
  "${REPO_ROOT}/lib/Memory" "${REPO_ROOT}/lib/MiniBidi" "${REPO_ROOT}/lib/Serialization" "${REPO_ROOT}/lib/hal"
  "${REPO_ROOT}/lib/I18n" "${REPO_ROOT}/freeink-sdk/libs/display/FreeInkDisplay/include")
target_compile_options(UiLayout_${name} PRIVATE -ffunction-sections -fdata-sections)
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  target_link_options(UiLayout_${name} PRIVATE -Wl,-dead_strip)
else()
  target_link_options(UiLayout_${name} PRIVATE -Wl,--gc-sections)
endif()
if(name STREQUAL "bitmap_rows")
  target_sources(UiLayout_${name} PRIVATE "${REPO_ROOT}/lib/GfxRenderer/Bitmap.cpp" "${REPO_ROOT}/lib/GfxRenderer/BitmapHelpers.cpp")
endif()
if(name STREQUAL "sleep_gray_planes")
  target_sources(UiLayout_${name} PRIVATE "${REPO_ROOT}/src/activities/boot_sleep/SleepGrayPlanes.cpp")
  target_compile_definitions(UiLayout_${name} PRIVATE REPO_ROOT_PATH="${REPO_ROOT}")
endif()
add_test(NAME UiLayout.${name} COMMAND UiLayout_${name})
endforeach()
