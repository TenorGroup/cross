find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(_icon57_sources
  "${REPO_ROOT}/src/components/TenorMenuChrome.cpp"
  "${REPO_ROOT}/src/activities/UiTabListActivity.cpp"
  "${REPO_ROOT}/src/activities/home/HomeActivity.cpp"
  "${REPO_ROOT}/src/activities/settings/TextSettingsActivity.cpp"
  "${REPO_ROOT}/src/activities/reader/EpubReaderMenuActivity.cpp"
  "${REPO_ROOT}/src/activities/reader/ReaderToolbarUi.cpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_icon57_sources})
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/extract.py"
  "${REPO_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/Production.inc"
  COMMAND_ERROR_IS_FATAL ANY)
foreach(_board X3 Touch)
  add_executable(Icon57${_board}Pixels harness.cpp)
  target_include_directories(Icon57${_board}Pixels PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}" "${REPO_ROOT}/src" "${REPO_ROOT}/lib/I18n"
    "${REPO_ROOT}/freeink-sdk/libs/assets/Icons/include"
    "${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/include")
  if(_board STREQUAL "Touch")
    target_compile_definitions(Icon57${_board}Pixels PRIVATE FREEINK_DEVICE_X4PRO=1)
  endif()
  add_test(NAME Icon57${_board}Pixels COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/check.py" "$<TARGET_FILE:Icon57${_board}Pixels>" "${_board}")
  add_test(NAME Icon57${_board}Alignment COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/alignment.py" "$<TARGET_FILE:Icon57${_board}Pixels>" "${_board}")
endforeach()
add_test(NAME Icon57UglyPixels COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/check.py" "$<TARGET_FILE:Icon57X3Pixels>" "Ugly")
add_test(NAME Icon57HeaderBytes COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/headers.py")
add_test(NAME Icon57SourceAlignment COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/alignment.py")
add_test(NAME Icon59GreyArt COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/grey.py")
add_test(NAME Icon59GreyBytes COMMAND "${Python3_EXECUTABLE}" "${REPO_ROOT}/scripts/icons/xam.py" --check)
add_test(NAME Icon59RoundStraight COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/round.py")
add_test(NAME Icon59SelectedBytes COMMAND "${Python3_EXECUTABLE}" "${REPO_ROOT}/scripts/icons/chon.py" --check)
