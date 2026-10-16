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
  ${REPO_ROOT}/lib/BlePageTurner/include
  ${REPO_ROOT}/lib/BlePageTurner/src
  ${REPO_ROOT}/.pio/libdeps/gh_release/ArduinoJson/src
)
target_compile_definitions(KoreaderAutoSyncTest PRIVATE ARDUINOJSON_ENABLE_ARDUINO_STRING=0 FREEINK_DEVICE_X3=1)
target_link_libraries(KoreaderAutoSyncTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(KoreaderAutoSyncTest)

set(BEFORE_OPEN_EXTRACT ${CMAKE_CURRENT_BINARY_DIR}/KOReaderBeforeOpen.inc)
add_custom_command(
  OUTPUT ${BEFORE_OPEN_EXTRACT}
  COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/extract_before_open.py
          ${REPO_ROOT}/src/KOReaderAutoSync.cpp ${BEFORE_OPEN_EXTRACT} beforeOpen
  DEPENDS ${REPO_ROOT}/src/KOReaderAutoSync.cpp ${CMAKE_CURRENT_SOURCE_DIR}/extract_before_open.py
  VERBATIM
)
add_executable(KOReaderBeforeOpenBehaviorTest
  BeforeOpenBehaviorTest.cpp
  ${BEFORE_OPEN_EXTRACT}
)
set_source_files_properties(${BEFORE_OPEN_EXTRACT} PROPERTIES HEADER_FILE_ONLY TRUE)
target_include_directories(KOReaderBeforeOpenBehaviorTest PRIVATE
  ${CMAKE_CURRENT_BINARY_DIR}
  ${REPO_ROOT}/lib/KOReaderSync
  ${REPO_ROOT}/lib/BlePageTurner/include
  ${REPO_ROOT}/lib/BlePageTurner/src
)
target_compile_definitions(KOReaderBeforeOpenBehaviorTest PRIVATE FREEINK_DEVICE_X3=1)
target_link_libraries(KOReaderBeforeOpenBehaviorTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(KOReaderBeforeOpenBehaviorTest)

set(PULL_GATED_EXTRACT ${CMAKE_CURRENT_BINARY_DIR}/KOReaderPullGated.inc)
set(NOTE_START_EXTRACT ${CMAKE_CURRENT_BINARY_DIR}/KOReaderNoteStart.inc)
set(PULL_RESTART_ACTIVITY_EXTRACT ${CMAKE_CURRENT_BINARY_DIR}/PullRestartActivity.inc)
set(AFTER_FIRST_FRAME_EXTRACT ${CMAKE_CURRENT_BINARY_DIR}/KOReaderAfterFirstFrame.inc)
add_custom_command(
  OUTPUT ${PULL_GATED_EXTRACT}
  COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/extract_before_open.py
          ${REPO_ROOT}/src/KOReaderAutoSync.cpp ${PULL_GATED_EXTRACT} pullGated
  DEPENDS ${REPO_ROOT}/src/KOReaderAutoSync.cpp ${CMAKE_CURRENT_SOURCE_DIR}/extract_before_open.py
  VERBATIM
)
add_custom_command(
  OUTPUT ${NOTE_START_EXTRACT}
  COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/extract_before_open.py
          ${REPO_ROOT}/src/KOReaderAutoSync.cpp ${NOTE_START_EXTRACT} noteStart
  DEPENDS ${REPO_ROOT}/src/KOReaderAutoSync.cpp ${CMAKE_CURRENT_SOURCE_DIR}/extract_before_open.py
  VERBATIM
)
add_custom_command(
  OUTPUT ${PULL_RESTART_ACTIVITY_EXTRACT}
  COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/extract_before_open.py
          ${REPO_ROOT}/src/activities/ActivityManager.cpp ${PULL_RESTART_ACTIVITY_EXTRACT} PullRestartActivity
  DEPENDS ${REPO_ROOT}/src/activities/ActivityManager.cpp ${CMAKE_CURRENT_SOURCE_DIR}/extract_before_open.py
  VERBATIM
)
add_custom_command(
  OUTPUT ${AFTER_FIRST_FRAME_EXTRACT}
  COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/extract_before_open.py
          ${REPO_ROOT}/src/KOReaderAutoSync.cpp ${AFTER_FIRST_FRAME_EXTRACT} afterFirstFrame
  DEPENDS ${REPO_ROOT}/src/KOReaderAutoSync.cpp ${CMAKE_CURRENT_SOURCE_DIR}/extract_before_open.py
  VERBATIM
)
add_executable(KOReaderRestartChainBehaviorTest
  RestartChainBehaviorTest.cpp
  ${BEFORE_OPEN_EXTRACT}
  ${PULL_GATED_EXTRACT}
  ${NOTE_START_EXTRACT}
  ${PULL_RESTART_ACTIVITY_EXTRACT}
  ${AFTER_FIRST_FRAME_EXTRACT}
)
set_source_files_properties(
  ${BEFORE_OPEN_EXTRACT}
  ${PULL_GATED_EXTRACT}
  ${NOTE_START_EXTRACT}
  ${PULL_RESTART_ACTIVITY_EXTRACT}
  ${AFTER_FIRST_FRAME_EXTRACT}
  PROPERTIES HEADER_FILE_ONLY TRUE
)
target_include_directories(KOReaderRestartChainBehaviorTest PRIVATE
  ${CMAKE_CURRENT_BINARY_DIR}
  ${REPO_ROOT}/lib/KOReaderSync
  ${REPO_ROOT}/lib/BlePageTurner/include
  ${REPO_ROOT}/lib/BlePageTurner/src
)
target_compile_definitions(KOReaderRestartChainBehaviorTest PRIVATE FREEINK_DEVICE_X3=1)
target_link_libraries(KOReaderRestartChainBehaviorTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(KOReaderRestartChainBehaviorTest)
