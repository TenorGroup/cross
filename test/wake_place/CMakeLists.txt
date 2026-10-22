add_executable(WakePlaceTest WakePlaceTest.cpp)
target_include_directories(WakePlaceTest PRIVATE ${REPO_ROOT}/src)
target_link_libraries(WakePlaceTest PRIVATE GTest::gtest_main)
gtest_discover_tests(WakePlaceTest)

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
