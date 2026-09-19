# Copy production sources unchanged so sibling quoted platform includes resolve
# to the host stubs. CMake tracks these inputs and recopies them after edits.
foreach(name ReadingStatsStore.cpp ReadingStatsStore.h)
  configure_file(${REPO_ROOT}/src/${name} ${CMAKE_CURRENT_BINARY_DIR}/${name} COPYONLY)
endforeach()
foreach(name BookStatsLibraryActivity.cpp BookStatsLibraryActivity.h)
  configure_file(${REPO_ROOT}/src/activities/home/${name} ${CMAKE_CURRENT_BINARY_DIR}/${name} COPYONLY)
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
