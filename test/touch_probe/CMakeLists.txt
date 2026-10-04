# Planned finger of the measurement build's CMD:TAP / CMD:SWIPE.
add_executable(TouchProbeTest TouchProbeTest.cpp)
target_link_libraries(TouchProbeTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(TouchProbeTest)
