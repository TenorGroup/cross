if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.16)
  project(cpfont_pack_tests NONE)
  enable_testing()
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)

if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
find_package(OpenSSL REQUIRED)
add_executable(FontPackInstallerTest FontPackInstallerTest.cpp
  ${REPO_ROOT}/src/FontPackInstaller.cpp
  ${REPO_ROOT}/src/FontInstaller.cpp
  ${REPO_ROOT}/lib/JsonParser/StreamingJsonParser.cpp
  ${REPO_ROOT}/test/font_installer/RegistryStubs.cpp
  ${REPO_ROOT}/test/font_installer/SettingsStubs.cpp)
target_include_directories(FontPackInstallerTest BEFORE PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs ${REPO_ROOT}/test/host_stubs
  ${REPO_ROOT}/test/font_installer/stubs)
target_include_directories(FontPackInstallerTest PRIVATE
  ${REPO_ROOT}/src ${REPO_ROOT}/lib/EpdFont ${REPO_ROOT}/lib/JsonParser
  ${REPO_ROOT}/lib/Serialization ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/BlePageTurner/include
  ${REPO_ROOT}/freeink-sdk/libs/hardware/SDCardManager/include
  ${REPO_ROOT}/.pio/libdeps/gh_release/ArduinoJson/src)
target_link_libraries(FontPackInstallerTest PRIVATE crosspoint_test_common GTest::gtest_main OpenSSL::Crypto)
gtest_discover_tests(FontPackInstallerTest)

add_test(NAME FontPackEntrypoints COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_entrypoints.py")
endif()

add_test(NAME FontPack COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/test_font_pack.py")
set_tests_properties(FontPack PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1" TIMEOUT 120)
add_test(NAME FontPackManifest COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/test_manifest.py")
set_tests_properties(FontPackManifest PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1" TIMEOUT 120)
