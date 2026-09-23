# Choices of the "Book + quotation" sleep screen that need no renderer: which quote is
# drawn, which body size it gets, the place line, and how a quote too long for the screen
# ends. The header under test is the one SleepActivity uses.
add_executable(SleepQuoteLayoutTest SleepQuoteLayoutTest.cpp)
target_include_directories(SleepQuoteLayoutTest PRIVATE ${REPO_ROOT}/src)
target_link_libraries(SleepQuoteLayoutTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(SleepQuoteLayoutTest)
