# tenor/ugly quotation page: line breaking, paging and the pick of the line of abuse, in integers.
add_executable(UglyQuotePageTest UglyQuotePageTest.cpp)
target_include_directories(UglyQuotePageTest PRIVATE ${REPO_ROOT}/src)
target_link_libraries(UglyQuotePageTest PRIVATE GTest::gtest_main)
gtest_discover_tests(UglyQuotePageTest)
