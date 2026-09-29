add_executable(TiltPickUpTest TiltPickUpTest.cpp ${REPO_ROOT}/lib/hal/HalTiltSensor.cpp)

target_include_directories(TiltPickUpTest PRIVATE
  stubs
  ${REPO_ROOT}/lib/hal
  ${REPO_ROOT}/freeink-sdk/libs/hardware/Imu/include
)

target_compile_definitions(TiltPickUpTest PRIVATE
  TILT_CLIPS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/x3-tilt-flicks-and-pick-ups.csv"
)

target_link_libraries(TiltPickUpTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(TiltPickUpTest)
