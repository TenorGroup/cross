# Automatic KOReader sync: the one gate for the sleep push and the wake pull, the push/pull
# decisions, and the list of books owed a push. Production sources, no stubs.
add_executable(KoreaderAutoSyncTest
  KoreaderAutoSyncTest.cpp
  PullResumeTest.cpp
  ${REPO_ROOT}/lib/KOReaderSync/KOReaderSyncDebt.cpp
  ${REPO_ROOT}/lib/KOReaderSync/KOReaderSyncMark.cpp
  ${REPO_ROOT}/lib/KOReaderSync/KOReaderWifiHint.cpp
  ${REPO_ROOT}/lib/KOReaderSync/ProgressComparison.cpp
)
target_include_directories(KoreaderAutoSyncTest PRIVATE
  ${REPO_ROOT}/lib/KOReaderSync
  ${REPO_ROOT}/.pio/libdeps/gh_release/ArduinoJson/src
)
target_compile_definitions(KoreaderAutoSyncTest PRIVATE ARDUINOJSON_ENABLE_ARDUINO_STRING=0)
target_link_libraries(KoreaderAutoSyncTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(KoreaderAutoSyncTest)
