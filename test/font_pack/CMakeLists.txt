find_package(OpenSSL REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
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
  ${REPO_ROOT}/.pio/libdeps/gh_release/ArduinoJson/src)
target_link_libraries(FontPackInstallerTest PRIVATE crosspoint_test_common GTest::gtest_main OpenSSL::Crypto)
gtest_discover_tests(FontPackInstallerTest)

add_test(NAME FontPackEntrypoints COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_entrypoints.py")
