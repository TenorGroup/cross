# Host tests for the FontInstaller name/path contract (S2/N3).
#
# The real src/FontInstaller.cpp is compiled into this target unchanged: the
# 31/87-byte bounds, the ASCII grammar, the root precedence and the
# "reject before touching storage" ordering are production logic.
#
# src/FontInstaller.cpp includes "CrossPointSettings.h" as a *sibling* quoted
# include, so no -I stub can stand in for it: the real src/CrossPointSettings.h
# and its real field layout are always the ones compiled. That needs the same
# include set test/mapped_input and test/tab_navigation already use:
#   test/host_stubs                  Arduino/WString/Print host shims
#   .pio/libdeps/gh_release           ArduinoJson (populated by `pio run -e gh_release`)
#   lib/Serialization                PersistableStore.h
#   lib/Epub/Epub                    ReaderRenderSpec.h
#   lib/EpdFont                      SdCardFontRegistry.h (real header, host impl)
# Stubbed for the host, and nothing else:
#   stubs/HalStorage.h               ESP SD storage API, call-recorded
#   stubs/Logging.h                  LOG_* no-ops
#   RegistryStubs.cpp                SdCardFontRegistry root lookups
#   SettingsStubs.cpp                CrossPointSettings::clearSdFontFamily()

set(PIO_LIBDEPS "${REPO_ROOT}/.pio/libdeps/gh_release")
set(ARDUINOJSON_SRC "${PIO_LIBDEPS}/ArduinoJson/src")
if(NOT IS_DIRECTORY "${ARDUINOJSON_SRC}")
  # Non-fatal so one missing host dependency cannot block every other test.
  message(WARNING
    "FontInstallerTest needs <ArduinoJson.h> from ${ARDUINOJSON_SRC}; run `pio run -e gh_release` once.")
endif()

add_executable(FontInstallerTest
  FontInstallerTest.cpp
  RegistryStubs.cpp
  SettingsStubs.cpp
  ${REPO_ROOT}/src/FontInstaller.cpp
)

# The host shims must win over every other include directory, as in the other
# host tests that compile production sources.
target_include_directories(FontInstallerTest BEFORE PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/test/host_stubs
)
target_include_directories(FontInstallerTest PRIVATE
  ${ARDUINOJSON_SRC}
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/Epub
)

target_link_libraries(FontInstallerTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(FontInstallerTest)
