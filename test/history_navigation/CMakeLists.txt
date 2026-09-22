if(NOT HISTORY_PRODUCTION_ROOT)
  set(HISTORY_PRODUCTION_ROOT "${REPO_ROOT}")
endif()
# Copy production sources unchanged so sibling quoted platform includes resolve
# to the host stubs. CMake tracks these inputs and recopies them after edits.
foreach(name ReadingStatsStore.cpp ReadingStatsStore.h)
  configure_file(${HISTORY_PRODUCTION_ROOT}/src/${name} ${CMAKE_CURRENT_BINARY_DIR}/${name} COPYONLY)
endforeach()
foreach(name BookStatsLibraryActivity.cpp BookStatsLibraryActivity.h)
  configure_file(${HISTORY_PRODUCTION_ROOT}/src/activities/home/${name} ${CMAKE_CURRENT_BINARY_DIR}/${name} COPYONLY)
endforeach()

add_executable(HistoryNavigationTest
  navigation.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/ReadingStatsStore.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/BookStatsLibraryActivity.cpp
  ${REPO_ROOT}/src/util/ReadingHabits.cpp
  ${REPO_ROOT}/src/util/SoLieuDoc.cpp
  ${REPO_ROOT}/src/util/NgayGio.cpp
)
target_include_directories(HistoryNavigationTest BEFORE PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${CMAKE_CURRENT_BINARY_DIR}
  ${REPO_ROOT}/src
  ${REPO_ROOT}/.pio/libdeps/gh_release/ArduinoJson/src
)
target_compile_definitions(HistoryNavigationTest PRIVATE ARDUINOJSON_ENABLE_ARDUINO_STRING=0)
add_test(NAME HistoryNavigationIntegration COMMAND HistoryNavigationTest)

# The fast-scan target includes the unchanged production .cpp so its local
# reader can accept a failing ArduinoJson allocator without firmware hooks.
add_executable(HistoryFastScanTest
  fast_scan.cpp
  ${REPO_ROOT}/src/util/ReadingHabits.cpp
  ${REPO_ROOT}/src/util/SoLieuDoc.cpp
  ${REPO_ROOT}/src/util/NgayGio.cpp
)
target_include_directories(HistoryFastScanTest BEFORE PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${CMAKE_CURRENT_BINARY_DIR}
  ${REPO_ROOT}/src
  ${REPO_ROOT}/.pio/libdeps/gh_release/ArduinoJson/src
)
target_compile_definitions(HistoryFastScanTest PRIVATE
  ARDUINOJSON_ENABLE_ARDUINO_STRING=0 HISTORY_HAS_OPEN_SNAPSHOT=1)
add_test(NAME HistoryFastScanHealthy COMMAND HistoryFastScanTest contract)
add_test(NAME HistoryFastScanReadFailures COMMAND HistoryFastScanTest faults)
add_test(NAME HistoryFastScanAllocationFailures COMMAND HistoryFastScanTest oom)

add_executable(ReadingStatsSchemaTest
  schema_preservation.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/ReadingStatsStore.cpp
  ${REPO_ROOT}/src/util/ReadingHabits.cpp
  ${REPO_ROOT}/src/util/SoLieuDoc.cpp
  ${REPO_ROOT}/src/util/NgayGio.cpp
)
target_include_directories(ReadingStatsSchemaTest BEFORE PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${CMAKE_CURRENT_BINARY_DIR}
  ${REPO_ROOT}/src
  ${REPO_ROOT}/.pio/libdeps/gh_release/ArduinoJson/src
)
target_compile_definitions(ReadingStatsSchemaTest PRIVATE ARDUINOJSON_ENABLE_ARDUINO_STRING=0)
add_test(NAME ReadingStatsSchemaPreservation COMMAND ReadingStatsSchemaTest)

add_executable(ReadingStatsIoTest
  io_preservation.cpp
  ${REPO_ROOT}/src/util/ReadingHabits.cpp
  ${REPO_ROOT}/src/util/SoLieuDoc.cpp
  ${REPO_ROOT}/src/util/NgayGio.cpp
)
target_include_directories(ReadingStatsIoTest BEFORE PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${CMAKE_CURRENT_BINARY_DIR}
  ${REPO_ROOT}/src
  ${REPO_ROOT}/.pio/libdeps/gh_release/ArduinoJson/src
)
target_compile_definitions(ReadingStatsIoTest PRIVATE ARDUINOJSON_ENABLE_ARDUINO_STRING=0)
add_test(NAME ReadingStatsIoPreservation COMMAND ReadingStatsIoTest)

add_executable(ReadingStatsResetFinalizationTest
  reset_finalization.cpp
  ${REPO_ROOT}/src/util/ReadingHabits.cpp
  ${REPO_ROOT}/src/util/SoLieuDoc.cpp
  ${REPO_ROOT}/src/util/NgayGio.cpp
)
target_include_directories(ReadingStatsResetFinalizationTest BEFORE PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${CMAKE_CURRENT_BINARY_DIR}
  ${REPO_ROOT}/src
  ${REPO_ROOT}/.pio/libdeps/gh_release/ArduinoJson/src
)
target_compile_definitions(ReadingStatsResetFinalizationTest PRIVATE ARDUINOJSON_ENABLE_ARDUINO_STRING=0)
add_test(NAME ReadingStatsResetFinalization COMMAND ReadingStatsResetFinalizationTest)
