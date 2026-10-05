if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.16)
  project(FrontlightGestureTests LANGUAGES CXX)
  enable_testing()
  get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
endif()
add_executable(frontlight_gesture state.cpp)
target_compile_features(frontlight_gesture PRIVATE cxx_std_17)
target_compile_options(frontlight_gesture PRIVATE -UNDEBUG)
add_test(NAME FrontlightGesture.state COMMAND frontlight_gesture)
add_executable(frontlight_renderer renderer.cpp
  "${REPO_ROOT}/lib/GfxRenderer/GfxRenderer.cpp"
  "${REPO_ROOT}/lib/GfxRenderer/FontCacheManager.cpp"
  "${REPO_ROOT}/lib/GfxRenderer/InlineSymbols.cpp"
  "${REPO_ROOT}/lib/EpdFont/EpdFont.cpp"
  "${REPO_ROOT}/lib/EpdFont/EpdFontFamily.cpp"
  "${REPO_ROOT}/lib/Utf8/Utf8.cpp"
  "${REPO_ROOT}/lib/Epub/Epub/blocks/TextBlock.cpp"
  "${REPO_ROOT}/lib/MiniBidi/BidiUtils.cpp"
  "${REPO_ROOT}/lib/MiniBidi/minibidi.c")
target_compile_features(frontlight_renderer PRIVATE cxx_std_20)
target_compile_options(frontlight_renderer PRIVATE -UNDEBUG -ffunction-sections -fdata-sections)
target_include_directories(frontlight_renderer BEFORE PRIVATE "${REPO_ROOT}/test/button_symbols/stubs")
target_include_directories(frontlight_renderer PRIVATE "${REPO_ROOT}/src" "${REPO_ROOT}/src/components"
  "${REPO_ROOT}/lib/EpdFont" "${REPO_ROOT}/lib/Utf8" "${REPO_ROOT}/lib/GfxRenderer" "${REPO_ROOT}/lib/Epub"
  "${REPO_ROOT}/lib/Memory" "${REPO_ROOT}/lib/MiniBidi" "${REPO_ROOT}/lib/Serialization" "${REPO_ROOT}/lib/hal"
  "${REPO_ROOT}/lib/I18n" "${REPO_ROOT}/freeink-sdk/libs/display/FreeInkDisplay/include")
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  target_link_options(frontlight_renderer PRIVATE -Wl,-dead_strip)
else()
  target_link_options(frontlight_renderer PRIVATE -Wl,--gc-sections)
endif()
add_test(NAME FrontlightGesture.renderer COMMAND frontlight_renderer)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME FrontlightGesture.usb_guard COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_usb_guard.py")
add_executable(frontlight_cancel cancel.cpp)
target_compile_features(frontlight_cancel PRIVATE cxx_std_17)
target_compile_options(frontlight_cancel PRIVATE -UNDEBUG)
target_include_directories(frontlight_cancel PRIVATE "${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/include")
add_test(NAME FrontlightGesture.cancel COMMAND frontlight_cancel)
target_sources(frontlight_cancel PRIVATE "${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/src/FreeInkUI.cpp")
add_test(NAME FrontlightGesture.deferred_home COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_deferred_home.py")
add_test(NAME FrontlightGesture.live_light COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_live_light.py")
