cmake_minimum_required(VERSION 3.16)
project(crosspoint_reader_tests C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(FetchContent)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

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
  ${REPO_ROOT}/lib/HttpHeader
  ${REPO_ROOT}/freeink-sdk/libs/network/BleKeyboardHost/include
)
target_compile_options(crosspoint_test_common INTERFACE
  -Wall
  -Wextra
  -pedantic
)

enable_testing()
include(GoogleTest)

add_test(NAME ReaderReturnPositionT4
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/reading_stats_simulator/test_quay_lai_t4_runtime_v1061.py)
add_test(NAME ReaderReturnReflowV1062
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/reading_stats_simulator/test_quay_lai_reflow_v1062.py)

add_test(NAME X4ProEveryScreenHasABar
  COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/x4pro_simulator/test_bang_thanh.py)

add_test(NAME KoreaderFavoritesRows
  COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/koreader_favorites/check.py ${REPO_ROOT})

add_test(NAME KoreaderAutoSyncSource
  COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/koreader_auto_sync/source_checks.py ${REPO_ROOT})

add_subdirectory(streaming_json_parser)
add_subdirectory(release_json_parser)
add_subdirectory(koreader_document_id)
add_subdirectory(ugly_font)
add_subdirectory(differential_rounding)
add_subdirectory(hyphenation_eval)
add_subdirectory(hyphenation_no_de)
add_subdirectory(utf8_compose)
add_subdirectory(opds_filename)
add_subdirectory(opds_feed_error)
add_subdirectory(opds_feed_alloc)
add_subdirectory(koreader_progress)
add_subdirectory(html_to_plain_text)
add_subdirectory(library_text)
add_subdirectory(library_format)
add_subdirectory(library_index_file)
add_subdirectory(library_builder)
add_subdirectory(dictionary_lookup_policy)
add_subdirectory(webdav_pull)
add_subdirectory(minibidi_arabic)
add_subdirectory(combining_marks)
add_subdirectory(credential_integrity)
add_subdirectory(xml_parser_utils)
add_subdirectory(token_boundary)
add_subdirectory(koreader_xpath_resolver)
add_subdirectory(progress_comparison)
add_subdirectory(koreader_auto_sync)
add_subdirectory(font_cache_manager)
add_subdirectory(ttf_ramp)
add_subdirectory(font_pack)
add_subdirectory(css_parser)
add_subdirectory(chapter_html_slim_parser)
add_subdirectory(book_search)
add_subdirectory(book_search_ui)
add_subdirectory(korean_line_breaking)
add_subdirectory(em_dash_line_breaking)
add_subdirectory(ligature_guard)
add_subdirectory(page_link)
add_subdirectory(quote_highlight)
add_subdirectory(quote_reselect)
add_subdirectory(sleep_quote)
add_subdirectory(word_select_repeat)
add_subdirectory(content_opf_parser)
add_subdirectory(chapter_position)
add_subdirectory(book_metadata_cache)
add_subdirectory(home_button)
add_subdirectory(ugly_shell)
add_subdirectory(desk_plane)
add_subdirectory(ugly_marks)
add_subdirectory(ugly_sleep_set)
add_subdirectory(ugly_quote_page)
add_subdirectory(scribble)
add_subdirectory(scribble_boundaries)
add_subdirectory(confirmation)
add_subdirectory(button_press_navigation)

add_subdirectory(inflate_stream)
add_subdirectory(zip_file)
add_subdirectory(fs_helpers)
add_subdirectory(file_browser_sort)
add_subdirectory(http_header)
add_subdirectory(txt_to_html)

add_subdirectory(absolute_grayscale)
add_subdirectory(mapped_input)
add_subdirectory(button_edge_latch)
add_subdirectory(wake_hold)
add_subdirectory(tab_navigation)
add_subdirectory(ugly_list_picture)
add_subdirectory(reader_menu_tabs)
add_subdirectory(tab_window)
add_subdirectory(touch_probe)
add_subdirectory(touch_shell)
add_subdirectory(icon57)
add_subdirectory(put_file)
add_subdirectory(cable_flash)
add_subdirectory(settings_tabs)
add_subdirectory(menu_value_choices)
add_subdirectory(settings_row_navigation)
add_subdirectory(ngay_gio)
add_subdirectory(so_lieu_doc)
add_subdirectory(lich_30)
add_subdirectory(reading_habits)
add_subdirectory(finish_estimate)

add_subdirectory(wake_book)
add_subdirectory(wake_place)
add_subdirectory(first_paint)
add_subdirectory(touch_scroll)
add_subdirectory(file_search_reveal)
add_subdirectory(text_settings_touch)
add_subdirectory(path_pill)
add_subdirectory(file_back_navigation)
add_subdirectory(x3_navigation)
add_subdirectory(frontlight_gesture)
add_subdirectory(reader_tap_zones)
add_subdirectory(x4_reader_menu)
add_subdirectory(jump_bar_clock)
add_subdirectory(utf8_middle_ellipsis)
add_subdirectory(status_glyphs)
add_subdirectory(rollback_probe)
add_subdirectory(cold_script)
add_subdirectory(hang_watch)

add_subdirectory(book_title_display)

add_subdirectory(web_path)

add_subdirectory(http_url)

add_subdirectory(webdav_replace)

add_subdirectory(secure_http)

add_subdirectory(secure_client)
add_subdirectory(builtin_ink)
add_subdirectory(ttf_ink)
add_subdirectory(reader_ink_repaint)

add_subdirectory(font_installer)

add_subdirectory(font_manifest)
add_subdirectory(font_download_heap)
add_subdirectory(font_download_progress)
add_subdirectory(font_store_names)
add_subdirectory(font_download_pack)
add_subdirectory(font_installed_picture)

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
add_subdirectory(pngdec_window)
add_subdirectory(cover_thumb_ram)
add_subdirectory(cover_jpeg_decode)
add_subdirectory(image_downscale)
add_subdirectory(sleep_cover_pxc)
add_subdirectory(bitmap_columns)
add_subdirectory(bmp_exit)
add_subdirectory(css_number)

add_subdirectory(keyboard_sync)
add_subdirectory(keyboard_layout_mask)

add_subdirectory(file_transfer_loop)
add_subdirectory(file_transfer_back_latch)
add_subdirectory(file_transfer_heap)
add_subdirectory(upload_socket)
add_subdirectory(ota_back_cancel)
add_subdirectory(ota_log)
add_subdirectory(ota_clean_boot)
add_subdirectory(silent_boot_paint)

add_subdirectory(sd_font_lifetime)
add_subdirectory(shed_parser)

add_subdirectory(storage_recovery)
add_subdirectory(hal_storage_trace)
add_subdirectory(progress_file)
add_subdirectory(card_rescue)

add_subdirectory(folder_rows)
add_subdirectory(habits_info_nav)
add_subdirectory(file_search)
add_subdirectory(file_search_helper)
add_subdirectory(text_relayout_quiet)

add_subdirectory(history_navigation)

add_subdirectory(section_cache)
add_subdirectory(reader_build_budget)
add_subdirectory(look_ahead_inputs)
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
add_subdirectory(x4c_reader_menu_default)
add_subdirectory(line_factor_pack)
add_subdirectory(gauge_capacity)
add_subdirectory(battery_display_smoothing)

add_subdirectory(http_downloader)

add_subdirectory(ble_integration)
add_subdirectory(ble_hold_connect)

add_subdirectory(settings_catalog)
add_subdirectory(overlay_snapshot)
add_subdirectory(menu_tilt_settings)
add_subdirectory(tenor_preset)
add_subdirectory(settings_upgrade)
add_subdirectory(reader_status_items)
add_subdirectory(settings_font_memory)

add_subdirectory(reading_stats_clock)

add_subdirectory(transfer_entry)

add_subdirectory(ui_tiers)
add_subdirectory(fixed_menu_chrome)

add_subdirectory(settings_v108)

add_executable(heap_big_probe heap_big_probe/HeapBigProbeTest.cpp)
target_compile_options(heap_big_probe PRIVATE -UNDEBUG)
target_include_directories(heap_big_probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src ${CMAKE_CURRENT_SOURCE_DIR}/../lib/Memory)
add_test(NAME heap_big_probe COMMAND heap_big_probe)

add_executable(heap_floor heap_floor/HeapFloorTest.cpp)
target_compile_options(heap_floor PRIVATE -UNDEBUG)
target_include_directories(heap_floor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../lib/Memory)
add_test(NAME heap_floor COMMAND heap_floor)

add_test(NAME heap_floor_gate COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/../scripts/cong_san.py)

add_test(NAME WebHtmlStripJs COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/web_html_strip/test_strip_js.py)
add_test(NAME WebHtmlStripTokens COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/web_html_strip/test_strip_tokens.py)
# Needs node on the host (like the freetype and numpy tests need theirs).
add_test(NAME WebFontFamilyNames COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/web_font_family/test_font_family.py)
add_test(NAME WebFontWeightLayout COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/web_font_weights/test_layout.py)
# Real Chrome against build_font_pack.py: skipped unless playwright-core is on NODE_PATH and Chrome is installed.
add_test(NAME WebFontWeightParity COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/web_font_weights/test_parity.py)
set_tests_properties(WebFontWeightParity PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 300)
add_test(NAME UiTextRules COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/ui_text_rules/check.py)
add_test(NAME I18nGeneration COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/i18n_generation/check.py)

add_executable(sheet_radio_plan sheet_radio_plan/SheetRadioPlanTest.cpp)
target_compile_options(sheet_radio_plan PRIVATE -UNDEBUG)
target_include_directories(sheet_radio_plan PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
add_test(NAME sheet_radio_plan COMMAND sheet_radio_plan)

add_executable(reader_ink_v108 reader_ink_v108/ReaderInkWeightTest.cpp)
target_compile_options(reader_ink_v108 PRIVATE -UNDEBUG)
target_include_directories(reader_ink_v108 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
add_test(NAME reader_ink_v108 COMMAND reader_ink_v108)

add_subdirectory(ui_layout)
add_subdirectory(button_symbols)

add_subdirectory(dropcap_raster)
add_subdirectory(wake_boot_memo)
add_subdirectory(frontlight_schedule)
add_subdirectory(recent_excerpt)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME SdFontVariantLayoutCache COMMAND "${CMAKE_COMMAND}" -E env
  "CROSSPOINT_SECTION_CACHE_TEST=$<TARGET_FILE:SectionCacheTest>"
  "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/reader_ink_v108/test_manager.py")
add_test(NAME ReaderFontDownloadNames COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/reader_ink_v108/test_download_manifest.py")
add_subdirectory(recent_actions)
add_subdirectory(image_probe_loan)
add_test(NAME FormatterCliSafety COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/clang_format_cli/test_cli.py")
add_test(NAME FirmwareImageSize COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/firmware_size/test_image_size.py")
add_test(NAME ReaderOpenHeapContract COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/reader_open_heap/test_contract.py" "${REPO_ROOT}")
add_test(NAME ReaderOpenHeapContractMutation COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/reader_open_heap/test_contract.py" "${REPO_ROOT}" --mutation)
set_tests_properties(ReaderOpenHeapContractMutation PROPERTIES WILL_FAIL TRUE)
add_test(NAME HeapFloorDoorSites COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/heap_floor/test_door_sites.py")
add_test(NAME WolfsslFlagWarnings COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/wolfssl_flags/test_wolfssl_warnings.py")
add_test(NAME LazyStatics COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/static_ram/test_lazy_statics.py")
add_test(NAME ProbeFlags COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/cable_tool/test_probe_flags.py")
add_test(NAME BuiltinFontMetadata COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/font_metadata/test_pooling.py")
add_test(NAME TiltSensorForegroundOwnership COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tilt_sensor_ownership/run.py" --source "${REPO_ROOT}")
add_test(NAME ReaderMenuTilt COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/reader_menu_tilt/run.py" --source "${REPO_ROOT}")
add_test(NAME ImuDoubleTap COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/imu_double_tap/run.py" --source "${REPO_ROOT}")
add_test(NAME BleRadioOwnership COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/ble_radio_owner/run.py" --source "${REPO_ROOT}")
add_test(NAME BleMenuUxRegression COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/ble_menu_ux/run.py" "${REPO_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/ble-menu-ux")
add_test(NAME InkQuickCycleContract COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/ink_quick_cycle/test_contract.py")
add_test(NAME MenuSleepSplitRegression COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/menu_sleep_split/run.py" --repo "${REPO_ROOT}" --output "${CMAKE_CURRENT_BINARY_DIR}/menu-sleep-split" --sanitize)
set_tests_properties(MenuSleepSplitRegression PROPERTIES RUN_SERIAL TRUE TIMEOUT 180)
add_test(NAME WiFiPowerProductionBehavior COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/wifi_power/run.py" --source-root "${REPO_ROOT}" --output "${CMAKE_CURRENT_BINARY_DIR}/wifi-power-behavior")

add_test(NAME PowerTimeoutProductionBehavior COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/power_timeouts/run.py" --source-root "${REPO_ROOT}" --output "${CMAKE_CURRENT_BINARY_DIR}/power-timeout-behavior")

add_subdirectory(gif_support)

add_subdirectory(sd_card_font)
add_subdirectory(sd_font_registry)
add_subdirectory(clipping_preview)
add_subdirectory(glyph_bitmap)

add_subdirectory(boot_trial_v1055)

add_test(NAME CoverRedriveProductionBehavior COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/cover_redrive/run.py" --repo "${REPO_ROOT}" --out "${CMAKE_CURRENT_BINARY_DIR}/cover-redrive")

add_subdirectory(text_gray_policy)

add_test(NAME SettingsStatusLabel COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/settings_semantics/status_label.py" --output "${CMAKE_CURRENT_BINARY_DIR}/settings-status-label")
add_test(NAME SettingsChoiceSemantics COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/settings_semantics/run.py" --output "${CMAKE_CURRENT_BINARY_DIR}/settings-semantics")
add_test(NAME SettingsChoicePixels COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/settings_semantics/pixels.py" --output "${CMAKE_CURRENT_BINARY_DIR}/settings-pixels")

add_test(NAME SettingsChoiceCallers COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/settings_semantics/choice_cues.py" --output "${CMAKE_CURRENT_BINARY_DIR}/settings-choice-callers")
add_test(NAME SettingsChoiceMenuInk COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/settings_semantics/popup.py" --output "${CMAKE_CURRENT_BINARY_DIR}/settings-menu-ink")
add_test(NAME SettingsX3Switch COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/settings_semantics/x3_switch.py" --output "${CMAKE_CURRENT_BINARY_DIR}/settings-x3-switch")

add_test(NAME SettingsX3Menu COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/settings_semantics/x3_menu.py" --output "${CMAKE_CURRENT_BINARY_DIR}/settings-x3-menu")

add_subdirectory(brand_boot)

add_subdirectory(tenor_ink)

add_subdirectory(progressive_jpeg)
add_subdirectory(progressive_luma)
add_subdirectory(idle_work)
add_subdirectory(cover_ink)
add_subdirectory(idle_home)

add_subdirectory(cover_holder)

add_subdirectory(home_thumb_work)
add_subdirectory(home_frame_strips)
add_subdirectory(gfx_region_rows)
add_subdirectory(bitmap_writer)
add_subdirectory(telex_composer)
add_subdirectory(file_browser_state)

# CrossPoint 2.0.0 suites.
add_subdirectory(clipping_store)
add_subdirectory(reader_session)
add_subdirectory(plugin_manifest_classification)
add_subdirectory(plugin_picker_layout)
add_subdirectory(protected_paths)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME RemoteButtonInputIntegration COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_remote_button_input.py)
add_test(NAME confirmation_translations COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_confirmation_translations.py)
add_test(NAME BleLearning COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_ble_learning.py)
add_subdirectory(ble_chapter_build_gate)
add_subdirectory(ble_remote_turn_gate)

add_test(NAME selection_buttons COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_selection_buttons.py)
add_test(NAME list_viewport COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_list_viewport.py)
add_test(NAME clipping_highlight_planes COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_clipping_highlight_planes.py)
add_test(NAME clipping_selection_text COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_clipping_selection_text.py)

add_test(NAME DictionaryJoinedDefinitionHeapDoor
  COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/dictionary_heap_door/check.py ${REPO_ROOT})
add_test(NAME PluginHttpResponseGrowsThroughTheHeapFloor
  COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/plugin_http_heap_door/check.py ${REPO_ROOT})
add_test(NAME PluginTokenFileIsWrittenAtomically
  COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/plugin_token_atomic/check.py ${REPO_ROOT})
add_test(NAME recursive_dictionary COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_recursive_dictionary.py)

# v1.1.0 merge suites: TXT position move to EPUB (D17), the idle chapter build stops on a key (D18),
# text layout with the tenor spacing levels (D5, D8, D21), KOReader requests unchanged from v1.0.76 (D4, D9),
# HalClock as the one SNTP owner (D12).
add_subdirectory(txt_position)
add_subdirectory(reader_yield_on_key)
add_subdirectory(parsed_text_layout)
add_subdirectory(koreader_wire)
add_subdirectory(trusted_time_halclock)
