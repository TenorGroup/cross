add_executable(WakePlaceTest WakePlaceTest.cpp)
target_include_directories(WakePlaceTest PRIVATE ${REPO_ROOT}/src)
target_link_libraries(WakePlaceTest PRIVATE GTest::gtest_main)
gtest_discover_tests(WakePlaceTest)

add_executable(WakeReturnTest WakeReturnTest.cpp)
target_include_directories(WakeReturnTest PRIVATE ${REPO_ROOT}/src)
target_link_libraries(WakeReturnTest PRIVATE GTest::gtest_main)
gtest_discover_tests(WakeReturnTest)

# state.json round trip of the place: the production CrossPointState with a host store and ArduinoJson (the copy
# PlatformIO fetched: .pio/libdeps/gh_release, or -DARDUINOJSON_ROOT=<ArduinoJson>).
if(NOT ARDUINOJSON_ROOT)
  set(ARDUINOJSON_ROOT "${REPO_ROOT}/.pio/libdeps/gh_release/ArduinoJson")
endif()
if(IS_DIRECTORY "${ARDUINOJSON_ROOT}/src")
  add_executable(WakeStateTest WakeStateTest.cpp ${REPO_ROOT}/src/CrossPointState.cpp)
  target_include_directories(WakeStateTest PRIVATE ${REPO_ROOT}/src ${REPO_ROOT}/test/reading_stats_clock/stubs
                                                   ${ARDUINOJSON_ROOT}/src)
  target_link_libraries(WakeStateTest PRIVATE GTest::gtest_main)
  gtest_discover_tests(WakeStateTest)
else()
  message(STATUS "WakeStateTest skipped: no ArduinoJson at ${ARDUINOJSON_ROOT}")
endif()

# The Connections wake as built from the production sources: the answers of the screens, the capture, the wake.
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME ConnectWake
  COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/connect_wake.py
          --repo ${REPO_ROOT} --output ${CMAKE_CURRENT_BINARY_DIR}/connect-wake --cxx ${CMAKE_CXX_COMPILER})

# The power-on reset of the touchless-power boards: the button only while it is still held (X4 Classic, X4 Pro).
add_test(NAME WakeReason
  COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/test_wake_reason.py)

# A silent restart that leaves a Settings tab comes back to it, and Back from a Wi-Fi task lands there (src/main.cpp).
add_test(NAME SilentReturnToSettings
  COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/silent_return.py --repo ${REPO_ROOT} --cxx ${CMAKE_CXX_COMPILER})

# The radio the plugin events brought up at sleep entry stays up for the KOReader push that is owed (D12).
add_test(NAME SleepWifiSharedWithThePush
  COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/sleep_wifi.py ${REPO_ROOT})
