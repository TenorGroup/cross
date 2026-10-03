# The power key's hold through a wake, watched while the boot goes on. Pure logic.
add_executable(WakeHoldTest WakeHoldTest.cpp)
target_include_directories(WakeHoldTest PRIVATE ${REPO_ROOT}/lib/hal)
target_link_libraries(WakeHoldTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(WakeHoldTest)
