set(PIO_LIBDEPS "${REPO_ROOT}/.pio/libdeps/gh_release")
set(ARDUINOJSON_SRC "${PIO_LIBDEPS}/ArduinoJson/src")
if(NOT IS_DIRECTORY "${ARDUINOJSON_SRC}")
  message(WARNING
    "FontManifestValidationTest needs <ArduinoJson.h> from ${ARDUINOJSON_SRC}; run `pio run -e gh_release` once.")
endif()

add_executable(FontManifestValidationTest FontManifestValidationTest.cpp)
target_include_directories(FontManifestValidationTest PRIVATE
  ${ARDUINOJSON_SRC}
  ${REPO_ROOT}/src/activities/settings
  ${REPO_ROOT}/freeink-sdk/libs/network/SecureNet/include
)
target_link_libraries(FontManifestValidationTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)
gtest_discover_tests(FontManifestValidationTest)
