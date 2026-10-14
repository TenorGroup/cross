# The production KOReaderSyncClient against a canned transport and fixed settings.
add_executable(KoreaderProgressTest
  KoreaderProgressTest.cpp
  ${REPO_ROOT}/lib/KOReaderSync/KOReaderSyncClient.cpp
)
target_include_directories(KoreaderProgressTest BEFORE PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/test/host_stubs
  ${REPO_ROOT}/lib/KOReaderSync
  ${REPO_ROOT}/lib/hal
  ${REPO_ROOT}/lib/NetworkTrust
  ${REPO_ROOT}/.pio/libdeps/gh_release/ArduinoJson/src
)
target_compile_definitions(KoreaderProgressTest PRIVATE ARDUINOJSON_ENABLE_ARDUINO_STRING=0)
target_link_libraries(KoreaderProgressTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(KoreaderProgressTest)
