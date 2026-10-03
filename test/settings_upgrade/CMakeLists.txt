cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(settings_upgrade LANGUAGES CXX)
  enable_testing()
endif()

if(NOT DEFINED REPO_ROOT)
  get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()

# Upgrade round trip: real settings.json files written by v1.0.18 and v1.0.19, plus an
# older pre-v1.0.18 file, loaded through the merged (CrossPoint 1.6.5) fromJson()/toJson().
# Reuses the settings_catalog/LinkStubs.cpp in-memory file double, same as test/tenor_preset.
add_executable(SettingsUpgradeTest
  SettingsUpgradeTest.cpp
  ${REPO_ROOT}/test/settings_catalog/LinkStubs.cpp
  ${REPO_ROOT}/src/CrossPointSettings.cpp
  ${REPO_ROOT}/src/ReaderFontSizes.cpp
  ${REPO_ROOT}/lib/I18n/I18n.cpp
  ${REPO_ROOT}/lib/I18n/I18nStrings.cpp
)
target_compile_features(SettingsUpgradeTest PRIVATE cxx_std_20)

target_include_directories(SettingsUpgradeTest BEFORE PRIVATE
  ${REPO_ROOT}/test/menu_tilt_settings/stubs
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
target_include_directories(SettingsUpgradeTest PRIVATE ${_inc_dirs})

target_compile_definitions(SettingsUpgradeTest PRIVATE
  FREEINK_DEVICE_X3=1
  FREEINK_DEVICE_X4=1
  FREEINK_CAP_FRONTLIGHT=0
  FREEINK_CAP_WARMLIGHT=0
  ENABLE_ARDUINO_FEATURES=0
  CROSSPOINT_VERSION="settings-upgrade-test"
  FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)

target_compile_options(SettingsUpgradeTest PRIVATE -ffunction-sections -fdata-sections)
if(APPLE)
  target_link_options(SettingsUpgradeTest PRIVATE -Wl,-dead_strip)
else()
  target_link_options(SettingsUpgradeTest PRIVATE -Wl,--gc-sections)
endif()

# One process per fixture: the settings object is a singleton.
add_test(NAME SettingsUpgradeV1019 COMMAND SettingsUpgradeTest v1019)
add_test(NAME SettingsUpgradeV1018 COMMAND SettingsUpgradeTest v1018)
add_test(NAME SettingsUpgradeOlder COMMAND SettingsUpgradeTest older)
add_test(NAME SettingsUpgradeTouchMigration COMMAND SettingsUpgradeTest touch-migration)
add_test(NAME SettingsUpgradeLegacyTheme COMMAND SettingsUpgradeTest legacy-theme)
add_test(NAME SettingsUpgradeShell COMMAND SettingsUpgradeTest shell)
