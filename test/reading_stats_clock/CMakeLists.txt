if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.16)
  project(ReadingStatsClockTests LANGUAGES CXX)
  enable_testing()
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT REPO_ROOT)
  set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
endif()
if(NOT ARDUINOJSON_ROOT)
  set(ARDUINOJSON_ROOT "${REPO_ROOT}/.pio/libdeps/gh_release/ArduinoJson")
endif()

# Copy the production store unchanged so its sibling platform includes resolve
# to this host harness. The clock itself is compiled from its production path.
foreach(name ReadingStatsStore.cpp ReadingStatsStore.h)
  configure_file(${REPO_ROOT}/src/${name} ${CMAKE_CURRENT_BINARY_DIR}/${name} COPYONLY)
endforeach()

add_executable(ReadingStatsClockTest
  ReadingStatsClockTest.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/ReadingStatsStore.cpp
  ${REPO_ROOT}/lib/hal/HalClock.cpp
  ${REPO_ROOT}/src/util/ReadingHabits.cpp
  ${REPO_ROOT}/src/util/SoLieuDoc.cpp
  ${REPO_ROOT}/src/util/NgayGio.cpp
)
target_include_directories(ReadingStatsClockTest BEFORE PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${CMAKE_CURRENT_BINARY_DIR}
  ${REPO_ROOT}/lib/hal
  ${REPO_ROOT}/src
  ${ARDUINOJSON_ROOT}/src
)
target_compile_definitions(ReadingStatsClockTest PRIVATE ARDUINOJSON_ENABLE_ARDUINO_STRING=0)
target_compile_options(ReadingStatsClockTest PRIVATE
  -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer)
target_link_options(ReadingStatsClockTest PRIVATE -fsanitize=address,undefined)

add_test(NAME ReadingStatsClockIntegration COMMAND ReadingStatsClockTest)
