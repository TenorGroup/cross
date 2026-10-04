# tenor/ugly sleep set: the doodles, sleep streams, i18n wake lines and what picks them.
find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_executable(UglySleepSetTest
  UglySleepSetTest.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${REPO_ROOT}/lib/miniz/src/miniz_impl.c
  ${REPO_ROOT}/lib/miniz/src/InflateStream.cpp
  ${REPO_ROOT}/lib/Memory/BuildScratch.cpp
  ${REPO_ROOT}/lib/hal/HalMemory.cpp
  ${REPO_ROOT}/lib/I18n/I18n.cpp
  ${REPO_ROOT}/lib/I18n/I18nStrings.cpp
)
target_include_directories(UglySleepSetTest PRIVATE
  ${REPO_ROOT}/test/inflate_stream/stubs
  ${REPO_ROOT}/src
  ${REPO_ROOT}/src/shells/ugly
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/hal
  ${REPO_ROOT}/lib/miniz/src
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/I18n
)
target_link_libraries(UglySleepSetTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(UglySleepSetTest)

add_test(NAME SleepSet.SleepSentenceCapacity
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/SleepSentenceCapacityTest.py)
