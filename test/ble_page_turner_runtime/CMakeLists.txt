# Bounded funnel test for every app-level BLE HID begin path.

add_executable(BlePageTurnerRuntimeTest
  BlePageTurnerRuntimeTest.cpp
  ${REPO_ROOT}/src/BlePageTurnerRuntime.cpp
)

target_include_directories(BlePageTurnerRuntimeTest BEFORE PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
)
target_include_directories(BlePageTurnerRuntimeTest PRIVATE
  ${REPO_ROOT}/src
)
target_compile_definitions(BlePageTurnerRuntimeTest PRIVATE
  FREEINK_CAP_BLE_HID_HOST=1
  REPO_ROOT_PATH="${REPO_ROOT}"
)
target_link_libraries(BlePageTurnerRuntimeTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)
gtest_discover_tests(BlePageTurnerRuntimeTest)

add_executable(BlePageTurnerRuntimeHardwareOffTest
  BlePageTurnerRuntimeHardwareOffTest.cpp
  ${REPO_ROOT}/src/BlePageTurnerRuntime.cpp
)
target_include_directories(BlePageTurnerRuntimeHardwareOffTest BEFORE PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
)
target_include_directories(BlePageTurnerRuntimeHardwareOffTest PRIVATE
  ${REPO_ROOT}/src
)
target_compile_definitions(BlePageTurnerRuntimeHardwareOffTest PRIVATE
  FREEINK_CAP_BLE_HID_HOST=0
)
target_link_libraries(BlePageTurnerRuntimeHardwareOffTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)
gtest_discover_tests(BlePageTurnerRuntimeHardwareOffTest)
