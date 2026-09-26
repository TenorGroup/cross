# When a heap in pieces keeps the page-turner radio off, and the one restart it may cost.
add_executable(BleHeapRestartTest BleHeapRestartTest.cpp)
target_include_directories(BleHeapRestartTest PRIVATE ${REPO_ROOT}/src)
target_link_libraries(BleHeapRestartTest PRIVATE GTest::gtest_main)
gtest_discover_tests(BleHeapRestartTest)
