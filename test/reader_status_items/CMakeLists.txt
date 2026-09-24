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

# The web settings handler, copied verbatim from the server source at configure time
# (re-run when that file changes) and compiled against a fake request.
set(_web_server ${REPO_ROOT}/src/network/CrossPointWebServer.cpp)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_web_server})
file(READ ${_web_server} _web_text)
string(FIND "${_web_text}" "void CrossPointWebServer::handlePostSettings() {" _web_start)
if(_web_start EQUAL -1)
  message(FATAL_ERROR "handlePostSettings() not found in ${_web_server}")
endif()
string(SUBSTRING "${_web_text}" ${_web_start} -1 _web_tail)
string(FIND "${_web_tail}" "\n}\n" _web_end)
math(EXPR _web_end "${_web_end} + 3")
string(SUBSTRING "${_web_tail}" 0 ${_web_end} _web_handler)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/handle_post_settings.inc "${_web_handler}")

add_executable(ReaderStatusItemsWebPostTest
  WebPostSettingsTest.cpp
  ${REPO_ROOT}/test/settings_catalog/LinkStubs.cpp
  ${REPO_ROOT}/src/CrossPointSettings.cpp
  ${REPO_ROOT}/src/ReaderFontSizes.cpp
  ${REPO_ROOT}/lib/I18n/I18n.cpp
  ${REPO_ROOT}/lib/I18n/I18nStrings.cpp
)
target_compile_features(ReaderStatusItemsWebPostTest PRIVATE cxx_std_20)
target_include_directories(ReaderStatusItemsWebPostTest BEFORE PRIVATE
  ${REPO_ROOT}/test/menu_tilt_settings/stubs
  ${REPO_ROOT}/test/host_stubs
  ${CMAKE_CURRENT_BINARY_DIR}
)
target_include_directories(ReaderStatusItemsWebPostTest PRIVATE ${_inc_dirs})
target_compile_definitions(ReaderStatusItemsWebPostTest PRIVATE
  FREEINK_DEVICE_X3=1
  FREEINK_DEVICE_X4=1
  FREEINK_CAP_FRONTLIGHT=0
  FREEINK_CAP_WARMLIGHT=0
  ENABLE_ARDUINO_FEATURES=0
  CROSSPOINT_VERSION="reader-status-items-test"
)
add_test(NAME ReaderStatusItemsWebPost COMMAND ReaderStatusItemsWebPostTest)
