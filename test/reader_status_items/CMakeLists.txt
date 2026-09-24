cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(reader_status_items LANGUAGES CXX)
  enable_testing()
endif()

if(NOT DEFINED REPO_ROOT)
  get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()

add_executable(ReaderStatusItemsTest
  ReaderStatusItemsTest.cpp
  ${REPO_ROOT}/test/settings_catalog/LinkStubs.cpp
  ${REPO_ROOT}/src/CrossPointSettings.cpp
  ${REPO_ROOT}/src/ReaderFontSizes.cpp
  ${REPO_ROOT}/lib/I18n/I18n.cpp
  ${REPO_ROOT}/lib/I18n/I18nStrings.cpp
)
target_compile_features(ReaderStatusItemsTest PRIVATE cxx_std_20)

target_include_directories(ReaderStatusItemsTest BEFORE PRIVATE
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
target_include_directories(ReaderStatusItemsTest PRIVATE ${_inc_dirs})

target_compile_definitions(ReaderStatusItemsTest PRIVATE
  FREEINK_DEVICE_X3=1
  FREEINK_DEVICE_X4=1
  FREEINK_CAP_FRONTLIGHT=0
  FREEINK_CAP_WARMLIGHT=0
  ENABLE_ARDUINO_FEATURES=0
  CROSSPOINT_VERSION="reader-status-items-test"
)

target_compile_options(ReaderStatusItemsTest PRIVATE -ffunction-sections -fdata-sections)
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  target_link_options(ReaderStatusItemsTest PRIVATE -Wl,-dead_strip)
else()
  target_link_options(ReaderStatusItemsTest PRIVATE -Wl,--gc-sections)
endif()

add_test(NAME ReaderStatusItems COMMAND ReaderStatusItemsTest)
