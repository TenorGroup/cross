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
add_subdirectory(library_text)
add_subdirectory(library_format)
add_subdirectory(library_index_file)
add_subdirectory(library_builder)
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
add_subdirectory(korean_line_breaking)
add_subdirectory(ligature_guard)
add_subdirectory(page_link)
add_subdirectory(quote_highlight)
add_subdirectory(quote_reselect)
add_subdirectory(sleep_quote)
add_subdirectory(word_select_repeat)
add_subdirectory(content_opf_parser)
add_subdirectory(chapter_position)
add_subdirectory(home_button)
add_subdirectory(ugly_shell)
add_subdirectory(ugly_sleep_set)

add_subdirectory(inflate_stream)
add_subdirectory(fs_helpers)

add_subdirectory(absolute_grayscale)
add_subdirectory(mapped_input)
add_subdirectory(button_edge_latch)
add_subdirectory(wake_hold)
add_subdirectory(tab_navigation)
add_subdirectory(reader_menu_tabs)
add_subdirectory(tab_window)
add_subdirectory(settings_tabs)
add_subdirectory(settings_row_navigation)
add_subdirectory(ngay_gio)
add_subdirectory(so_lieu_doc)
add_subdirectory(lich_30)
add_subdirectory(reading_habits)
add_subdirectory(finish_estimate)

add_subdirectory(wake_book)
add_subdirectory(rollback_probe)
add_subdirectory(cold_script)

add_subdirectory(book_metadata_cache)

add_subdirectory(web_path)

add_subdirectory(http_url)

add_subdirectory(webdav_replace)

add_subdirectory(secure_http)

add_subdirectory(secure_client)

add_subdirectory(font_installer)

add_subdirectory(font_manifest)
add_subdirectory(font_download_progress)

# Library-local host suites live next to the code they test; the binary directory
# keeps them out of the flat test/ tree.
add_subdirectory(${REPO_ROOT}/freeink-sdk/libs/network/BleKeyboardHost/tests BleKeyboardHostTests)

# BTH2: duong page-turner BLE voi dau vao gia (report HID that -> luot lat trang).
add_subdirectory(ble_page_turner)
# The page turner module's own suite (it also runs alone, see its CMakeLists). Offline, its
# ArduinoJson comes from the copy PlatformIO already fetched when there is one.
if(NOT FETCHCONTENT_SOURCE_DIR_ARDUINOJSON AND IS_DIRECTORY "${REPO_ROOT}/.pio/libdeps/gh_release/ArduinoJson/src")
  set(FETCHCONTENT_SOURCE_DIR_ARDUINOJSON "${REPO_ROOT}/.pio/libdeps/gh_release/ArduinoJson")
endif()
add_subdirectory(${REPO_ROOT}/lib/BlePageTurner/test BlePageTurnerTests)
add_subdirectory(truncated_text)
add_subdirectory(png_bmp_converter)
add_subdirectory(cover_thumb_ram)
add_subdirectory(cover_jpeg_decode)
add_subdirectory(image_downscale)
add_subdirectory(css_number)

add_subdirectory(keyboard_sync)
add_subdirectory(keyboard_layout_mask)

add_subdirectory(file_transfer_loop)
add_subdirectory(file_transfer_back_latch)
add_subdirectory(upload_socket)
add_subdirectory(ota_back_cancel)
add_subdirectory(ota_log)
add_subdirectory(ota_clean_boot)
add_subdirectory(silent_boot_paint)

add_subdirectory(sd_font_lifetime)
add_subdirectory(shed_parser)

add_subdirectory(storage_recovery)
add_subdirectory(hal_storage_trace)

add_subdirectory(folder_rows)

add_subdirectory(history_navigation)

add_subdirectory(section_cache)
add_subdirectory(reader_build_budget)
add_subdirectory(reader_turn_trace)

add_subdirectory(cache_serialization)

add_subdirectory(webdav_handler_behavior)

add_subdirectory(opf_cache_io)
add_subdirectory(huge_book)
add_subdirectory(sdfat_alloc)
add_subdirectory(thumb_name)
add_subdirectory(cover_ref)
add_subdirectory(zip_central_dir)

add_subdirectory(txt_zip_io)

add_subdirectory(clock_ntp_behavior)

add_subdirectory(x4_sleep_clock)
add_subdirectory(gauge_capacity)
add_subdirectory(battery_display_smoothing)

add_subdirectory(http_downloader)

add_subdirectory(ble_integration)

add_subdirectory(settings_catalog)
add_subdirectory(menu_tilt_settings)
add_subdirectory(tenor_preset)
add_subdirectory(settings_upgrade)
add_subdirectory(reader_status_items)
add_subdirectory(settings_font_memory)

add_subdirectory(reading_stats_clock)

add_subdirectory(transfer_entry)

add_subdirectory(ui_tiers)

add_subdirectory(settings_v108)

add_executable(reader_ink_v108 reader_ink_v108/ReaderInkWeightTest.cpp)
target_compile_options(reader_ink_v108 PRIVATE -UNDEBUG)
target_include_directories(reader_ink_v108 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
add_test(NAME reader_ink_v108 COMMAND reader_ink_v108)

add_subdirectory(ui_layout)
add_subdirectory(button_symbols)

add_subdirectory(dropcap_raster)
add_subdirectory(wake_boot_memo)
add_subdirectory(recent_excerpt)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME FormatterCliSafety COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/clang_format_cli/test_cli.py")
add_test(NAME FirmwareImageSize COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/firmware_size/test_image_size.py")
add_test(NAME BuiltinFontMetadata COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/font_metadata/test_pooling.py")
add_test(NAME TiltSensorForegroundOwnership COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tilt_sensor_ownership/run.py" --source "${REPO_ROOT}")
add_test(NAME ImuDoubleTap COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/imu_double_tap/run.py" --source "${REPO_ROOT}")
add_test(NAME BleRadioOwnership COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/ble_radio_owner/run.py" --source "${REPO_ROOT}")
add_test(NAME BleMenuUxRegression COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/ble_menu_ux/run.py" "${REPO_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/ble-menu-ux")
add_test(NAME InkQuickCycleContract COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/ink_quick_cycle/test_contract.py")
add_test(NAME MenuSleepSplitRegression COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/menu_sleep_split/run.py" --repo "${REPO_ROOT}" --output "${CMAKE_CURRENT_BINARY_DIR}/menu-sleep-split" --sanitize)
set_tests_properties(MenuSleepSplitRegression PROPERTIES RUN_SERIAL TRUE TIMEOUT 180)
add_test(NAME WiFiPowerProductionBehavior COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/wifi_power/run.py" --source-root "${REPO_ROOT}" --output "${CMAKE_CURRENT_BINARY_DIR}/wifi-power-behavior")

add_test(NAME PowerTimeoutProductionBehavior COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/power_timeouts/run.py" --source-root "${REPO_ROOT}" --output "${CMAKE_CURRENT_BINARY_DIR}/power-timeout-behavior")

add_subdirectory(sd_card_font)
add_subdirectory(glyph_bitmap)
