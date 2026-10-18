# The restart for a card that stopped answering: the probe, the decision and the boot chain.
add_executable(CardRescueTest CardRescueTest.cpp)
target_link_libraries(CardRescueTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(CardRescueTest)
