# The one-line view of a search hit (src/components/BookSearchLayout.h) is pure; no device header comes in.
add_executable(BookSearchLayoutTest BookSearchLayoutTest.cpp)
target_include_directories(BookSearchLayoutTest PRIVATE ${REPO_ROOT}/src/components)
target_compile_options(BookSearchLayoutTest PRIVATE -Wall -Wextra -Werror)
target_link_libraries(BookSearchLayoutTest PRIVATE GTest::gtest_main)
gtest_discover_tests(BookSearchLayoutTest)
