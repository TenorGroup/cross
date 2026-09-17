cmake_minimum_required(VERSION 3.16)
project(crosspoint_reader_tests C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(FetchContent)

FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.17.0
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

add_library(crosspoint_test_common INTERFACE)
target_include_directories(crosspoint_test_common INTERFACE
  ${REPO_ROOT}
  ${REPO_ROOT}/lib
)
target_compile_options(crosspoint_test_common INTERFACE
  -Wall
  -Wextra
  -pedantic
)

enable_testing()
include(GoogleTest)

add_subdirectory(streaming_json_parser)
add_subdirectory(release_json_parser)
add_subdirectory(differential_rounding)
add_subdirectory(hyphenation_eval)
add_subdirectory(utf8_compose)
add_subdirectory(opds_filename)
add_subdirectory(html_to_plain_text)
add_subdirectory(minibidi_arabic)
add_subdirectory(combining_marks)
add_subdirectory(credential_integrity)
add_subdirectory(xml_parser_utils)
add_subdirectory(token_boundary)
add_subdirectory(koreader_xpath_resolver)
add_subdirectory(progress_comparison)
add_subdirectory(font_cache_manager)
add_subdirectory(css_parser)
add_subdirectory(chapter_html_slim_parser)
add_subdirectory(ligature_guard)
add_subdirectory(page_link)
add_subdirectory(chapter_position)

add_subdirectory(inflate_stream)
add_subdirectory(fs_helpers)

add_subdirectory(absolute_grayscale)
add_subdirectory(mapped_input)
add_subdirectory(tab_navigation)
add_subdirectory(reader_menu_tabs)
add_subdirectory(tab_window)
add_subdirectory(settings_tabs)
add_subdirectory(ngay_gio)
add_subdirectory(so_lieu_doc)
add_subdirectory(reading_habits)
add_subdirectory(wake_buttons)

add_subdirectory(book_metadata_cache)

add_subdirectory(web_path)

add_subdirectory(http_url)

add_subdirectory(webdav_replace)

add_subdirectory(secure_http)

add_subdirectory(secure_client)

add_subdirectory(font_installer)

add_subdirectory(font_manifest)

# Library-local host suites live next to the code they test; the binary directory
# keeps them out of the flat test/ tree.
add_subdirectory(${REPO_ROOT}/freeink-sdk/libs/network/BleKeyboardHost/tests BleKeyboardHostTests)

# BTH2: duong page-turner BLE voi dau vao gia (report HID that -> luot lat trang).
add_subdirectory(ble_page_turner)
add_subdirectory(ble_page_turner_runtime)
add_subdirectory(truncated_text)
add_subdirectory(png_bmp_converter)
