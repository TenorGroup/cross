cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(line_factor_pack LANGUAGES CXX)
  enable_testing()
endif()

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
if(NOT DEFINED SETTINGS_SOURCE_DIR)
  set(SETTINGS_SOURCE_DIR "${REPO_ROOT}/src")
endif()

set(TEST_SOURCES
  LineFactorPackTest.cpp
  ${REPO_ROOT}/test/settings_catalog/LinkStubs.cpp
  ${SETTINGS_SOURCE_DIR}/CrossPointSettings.cpp
  ${REPO_ROOT}/src/ReaderFontSizes.cpp
  ${REPO_ROOT}/lib/I18n/I18n.cpp
  ${REPO_ROOT}/lib/I18n/I18nStrings.cpp
)

file(GLOB_RECURSE ALL_DIRS LIST_DIRECTORIES true "${REPO_ROOT}/lib/*" "${REPO_ROOT}/freeink-sdk/*"
     "${REPO_ROOT}/src/*")
file(GLOB LIBDEP_SRCS LIST_DIRECTORIES true "${REPO_ROOT}/.pio/libdeps/gh_release/*/src")
set(INCLUDE_DIRS "${REPO_ROOT}/src")
foreach(DIR ${LIBDEP_SRCS})
  if(IS_DIRECTORY ${DIR})
    list(APPEND INCLUDE_DIRS ${DIR})
  endif()
endforeach()
foreach(DIR ${ALL_DIRS})
  if(IS_DIRECTORY ${DIR} AND NOT ${DIR} MATCHES "pro_stubs" AND NOT ${DIR} MATCHES "/test/host")
    list(APPEND INCLUDE_DIRS ${DIR})
  endif()
endforeach()
list(REMOVE_DUPLICATES INCLUDE_DIRS)

function(add_line_factor_target target board_macro)
  add_executable(${target} ${TEST_SOURCES})
  target_compile_features(${target} PRIVATE cxx_std_20)
  target_include_directories(${target} BEFORE PRIVATE
    ${REPO_ROOT}/test/menu_tilt_settings/stubs
    ${REPO_ROOT}/test/host_stubs
    ${SETTINGS_SOURCE_DIR}
  )
  target_include_directories(${target} PRIVATE ${INCLUDE_DIRS})
  target_compile_definitions(${target} PRIVATE
    ${board_macro}=1
    FREEINK_CAP_FRONTLIGHT=0
    FREEINK_CAP_WARMLIGHT=0
    ENABLE_ARDUINO_FEATURES=0
    CROSSPOINT_VERSION="line-factor-pack-test"
  )
  target_compile_options(${target} PRIVATE -ffunction-sections -fdata-sections)
  if(APPLE)
    target_link_options(${target} PRIVATE -Wl,-dead_strip)
  else()
    target_link_options(${target} PRIVATE -Wl,--gc-sections)
  endif()

  add_test(NAME ${target} COMMAND ${target})
endfunction()

add_line_factor_target(LineFactorPackX3 FREEINK_DEVICE_X3)
add_line_factor_target(LineFactorPackX4Pro FREEINK_DEVICE_X4PRO)
