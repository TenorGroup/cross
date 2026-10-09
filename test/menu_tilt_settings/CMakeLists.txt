cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(menu_tilt_settings LANGUAGES CXX)
  enable_testing()
endif()

if(NOT DEFINED REPO_ROOT)
  get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()

add_executable(MenuTiltSettingsTest
  MenuTiltSettingsTest.cpp
  ${REPO_ROOT}/test/settings_catalog/LinkStubs.cpp
  ${REPO_ROOT}/src/CrossPointSettings.cpp
  ${REPO_ROOT}/src/ReaderFontSizes.cpp
  ${REPO_ROOT}/lib/I18n/I18n.cpp
  ${REPO_ROOT}/lib/I18n/I18nStrings.cpp
)
target_compile_features(MenuTiltSettingsTest PRIVATE cxx_std_20)

target_include_directories(MenuTiltSettingsTest BEFORE PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/test/host_stubs
)

file(GLOB_RECURSE _all_dirs LIST_DIRECTORIES true "${REPO_ROOT}/lib/*" "${REPO_ROOT}/freeink-sdk/*" "${REPO_ROOT}/src/*")
file(GLOB _libdep_srcs LIST_DIRECTORIES true "${REPO_ROOT}/.pio/libdeps/gh_release/*/src")
set(_inc_dirs "${REPO_ROOT}/src")
foreach(_d ${_libdep_srcs})
  if(IS_DIRECTORY ${_d})
    list(APPEND _inc_dirs ${_d})
  endif()
endforeach()
foreach(_d ${_all_dirs})
  if(IS_DIRECTORY ${_d} AND NOT ${_d} MATCHES "pro_stubs" AND NOT ${_d} MATCHES "/test/host")
    list(APPEND _inc_dirs ${_d})
  endif()
endforeach()
list(REMOVE_DUPLICATES _inc_dirs)
target_include_directories(MenuTiltSettingsTest PRIVATE ${_inc_dirs})

target_compile_definitions(MenuTiltSettingsTest PRIVATE
  FREEINK_DEVICE_X3=1
  FREEINK_DEVICE_X4=1
  FREEINK_CAP_FRONTLIGHT=0
  FREEINK_CAP_WARMLIGHT=0
  ENABLE_ARDUINO_FEATURES=0
  CROSSPOINT_VERSION="menu-tilt-settings-test"
)

target_compile_options(MenuTiltSettingsTest PRIVATE -ffunction-sections -fdata-sections)
if(APPLE)
  target_link_options(MenuTiltSettingsTest PRIVATE -Wl,-dead_strip)
else()
  target_link_options(MenuTiltSettingsTest PRIVATE -Wl,--gc-sections)
endif()

add_test(NAME MenuTiltSettingsNoImu COMMAND MenuTiltSettingsTest no-imu)
add_test(NAME MenuTiltSettingsImu COMMAND MenuTiltSettingsTest imu)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME UglyTiltNavigation COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/test/ugly_shell/tilt_navigation.py
  --repo ${REPO_ROOT} --output ${CMAKE_CURRENT_BINARY_DIR}/ugly-tilt)
add_test(NAME PowerButtonTapWake COMMAND MenuTiltSettingsTest power)
add_test(NAME QuickActionPerScreen COMMAND MenuTiltSettingsTest action)
add_test(NAME ShakeSettingsImu COMMAND MenuTiltSettingsTest shake)
add_test(NAME FlipSettingsImu COMMAND MenuTiltSettingsTest flip)
add_test(NAME DoubleTapSettingsImu COMMAND MenuTiltSettingsTest double-tap)
add_test(NAME PowerValuesKept COMMAND MenuTiltSettingsTest power-values)
