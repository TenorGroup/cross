# tenor/ugly quotation page: line breaking, paging, the pick of the line of abuse and the scribbles, in integers.
add_executable(UglyQuotePageTest UglyQuotePageTest.cpp UglyScrapTest.cpp)
target_include_directories(UglyQuotePageTest PRIVATE ${REPO_ROOT}/src)
target_link_libraries(UglyQuotePageTest PRIVATE GTest::gtest_main)
gtest_discover_tests(UglyQuotePageTest)
