cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(tenor_preset LANGUAGES CXX)
  enable_testing()
endif()

if(NOT DEFINED REPO_ROOT)
  get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()

# The reading setup through the real settings.json code: the in-memory file of
# settings_catalog/LinkStubs.cpp lets a load resave and the next load read it back.
add_executable(TenorPresetTest
  TenorPresetTest.cpp
  ${REPO_ROOT}/test/settings_catalog/LinkStubs.cpp
  ${REPO_ROOT}/src/CrossPointSettings.cpp
  ${REPO_ROOT}/src/ReaderFontSizes.cpp
  ${REPO_ROOT}/lib/I18n/I18n.cpp
  ${REPO_ROOT}/lib/I18n/I18nStrings.cpp
)
target_compile_features(TenorPresetTest PRIVATE cxx_std_20)

target_include_directories(TenorPresetTest BEFORE PRIVATE
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
target_include_directories(TenorPresetTest PRIVATE ${_inc_dirs})

target_compile_definitions(TenorPresetTest PRIVATE
  FREEINK_DEVICE_X3=1
  FREEINK_DEVICE_X4=1
  FREEINK_CAP_FRONTLIGHT=0
  FREEINK_CAP_WARMLIGHT=0
  ENABLE_ARDUINO_FEATURES=0
  CROSSPOINT_VERSION="tenor-preset-test"
)

target_compile_options(TenorPresetTest PRIVATE -ffunction-sections -fdata-sections)
if(APPLE)
  target_link_options(TenorPresetTest PRIVATE -Wl,-dead_strip)
else()
  target_link_options(TenorPresetTest PRIVATE -Wl,--gc-sections)
endif()

# One process per case: the settings object is a singleton, and a blank card
# has to be read before anything else touched it.
add_test(NAME TenorPresetBlankCard COMMAND TenorPresetTest blank)
add_test(NAME TenorPresetOldFile COMMAND TenorPresetTest old-file)
add_test(NAME TenorPresetOnlyOnce COMMAND TenorPresetTest once)
add_test(NAME TenorPresetOldSpacingVersion COMMAND TenorPresetTest old-spacing)
add_test(NAME TenorPresetNoImu COMMAND TenorPresetTest no-imu)

# The same code built for Paper Mono, whose power key reaches the firmware only as a click.
add_executable(TenorPresetPaperMonoTest
  TenorPresetTest.cpp
  ${REPO_ROOT}/test/settings_catalog/LinkStubs.cpp
  ${REPO_ROOT}/src/CrossPointSettings.cpp
  ${REPO_ROOT}/src/ReaderFontSizes.cpp
  ${REPO_ROOT}/lib/I18n/I18n.cpp
  ${REPO_ROOT}/lib/I18n/I18nStrings.cpp
)
target_compile_features(TenorPresetPaperMonoTest PRIVATE cxx_std_20)
target_include_directories(TenorPresetPaperMonoTest BEFORE PRIVATE
  ${REPO_ROOT}/test/menu_tilt_settings/stubs
  ${REPO_ROOT}/test/host_stubs
)
target_include_directories(TenorPresetPaperMonoTest PRIVATE ${_inc_dirs})
target_compile_definitions(TenorPresetPaperMonoTest PRIVATE
  FREEINK_DEVICE_PAPERMONO=1
  FREEINK_CAP_FRONTLIGHT=0
  FREEINK_CAP_WARMLIGHT=0
  ENABLE_ARDUINO_FEATURES=0
  CROSSPOINT_VERSION="tenor-preset-test"
)
target_compile_options(TenorPresetPaperMonoTest PRIVATE -ffunction-sections -fdata-sections)
if(APPLE)
  target_link_options(TenorPresetPaperMonoTest PRIVATE -Wl,-dead_strip)
else()
  target_link_options(TenorPresetPaperMonoTest PRIVATE -Wl,--gc-sections)
endif()
add_test(NAME TenorPresetPaperMonoPowerClick COMMAND TenorPresetPaperMonoTest papermono)
