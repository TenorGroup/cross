cmake_minimum_required(VERSION 3.16)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(fixed_menu_chrome NONE)
  enable_testing()
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME FixedMenuChrome.FontRetry
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/font_retry.py"
          --work "${CMAKE_CURRENT_BINARY_DIR}/font-retry")
add_test(NAME FixedMenuChrome.LocalizedErrors
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/localized_errors.py"
          --work "${CMAKE_CURRENT_BINARY_DIR}/localized-errors")
add_test(NAME FixedMenuChrome.SettingsPairs
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/settings_pairs.py"
          --work "${CMAKE_CURRENT_BINARY_DIR}/settings-pairs")
add_test(NAME FixedMenuChrome.BleLayout
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/ble_layout.py"
          --work "${CMAKE_CURRENT_BINARY_DIR}/ble-layout")
add_test(NAME FixedMenuChrome.FavoriteRoot
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/favorite_root.py"
          --work "${CMAKE_CURRENT_BINARY_DIR}/favorite-root")
add_test(NAME FixedMenuChrome.FooterSpacing
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run.py"
          --footer-spacing --work "${CMAKE_CURRENT_BINARY_DIR}/footer-spacing")
add_test(NAME FixedMenuChrome.AllScreenBars
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/all_bars.py"
          --work "${CMAKE_CURRENT_BINARY_DIR}/all-bars")
add_test(NAME FixedMenuChrome.FormsNetworkKeyboard
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run.py"
          --forms --work "${CMAKE_CURRENT_BINARY_DIR}/forms")
add_test(NAME FixedMenuChrome.FileReaderQuotes
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run.py"
          --phase2 --work "${CMAKE_CURRENT_BINARY_DIR}/phase2")
add_test(NAME FixedMenuChrome.X3ScreenLayout
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run.py"
          --work "${CMAKE_CURRENT_BINARY_DIR}/layout")
add_test(NAME FixedMenuChrome.UglyScreenLayout
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run.py"
          --ugly-contract --work "${CMAKE_CURRENT_BINARY_DIR}/ugly-layout")
add_test(NAME FixedMenuChrome.UglyStatusBar
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run.py"
          --ugly-status-contract --work "${CMAKE_CURRENT_BINARY_DIR}/ugly-status")
foreach(shell touch ugly)
  add_test(NAME FixedMenuChrome.${shell}Gate
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run.py"
            --shell "${shell}" --work "${CMAKE_CURRENT_BINARY_DIR}/${shell}")
endforeach()
