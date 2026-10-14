add_library(book_search_core STATIC
  ${REPO_ROOT}/lib/BookSearch/BookSearch.cpp
  ${REPO_ROOT}/lib/Epub/Epub/htmlEntities.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${REPO_ROOT}/lib/expat/xmlparse.c
  ${REPO_ROOT}/lib/expat/xmlrole.c
  ${REPO_ROOT}/lib/expat/xmltok.c)
target_include_directories(book_search_core PUBLIC
  ${REPO_ROOT}/lib/BookSearch ${REPO_ROOT}/lib/Epub ${REPO_ROOT}/lib/Utf8 ${REPO_ROOT}/lib/expat)
target_compile_definitions(book_search_core PRIVATE XML_CONTEXT_BYTES=1024 XML_GE=0)
target_compile_options(book_search_core PRIVATE -Wall -Wextra -Werror)

get_target_property(parser_sources ChapterHtmlSlimParserTest SOURCES)
list(REMOVE_ITEM parser_sources ChapterHtmlSlimParserTest.cpp ${REPO_ROOT}/lib/Utf8/Utf8.cpp)
list(TRANSFORM parser_sources PREPEND "${REPO_ROOT}/test/chapter_html_slim_parser/"
     REGEX "^[^/].*")
get_target_property(parser_includes ChapterHtmlSlimParserTest INCLUDE_DIRECTORIES)
add_executable(BookSearchTest BookSearchTest.cpp ${parser_sources})
target_include_directories(BookSearchTest PRIVATE ${parser_includes})
target_link_libraries(BookSearchTest PRIVATE book_search_core GTest::gtest_main crosspoint_test_common)
gtest_discover_tests(BookSearchTest)

foreach(board IN ITEMS Psram X3)
  add_executable(BookSearchRunner${board}Test RunnerTest.cpp ${REPO_ROOT}/src/BookSearchRunner.cpp)
  target_include_directories(BookSearchRunner${board}Test BEFORE PRIVATE
    stubs ${REPO_ROOT}/test/chapter_html_slim_parser/stubs ${REPO_ROOT}/src ${REPO_ROOT}/lib/Memory)
  target_link_libraries(BookSearchRunner${board}Test PRIVATE book_search_core GTest::gtest_main)
  target_compile_options(BookSearchRunner${board}Test PRIVATE -Wall -Wextra -Werror)
  if(board STREQUAL "Psram")
    target_compile_definitions(BookSearchRunner${board}Test PRIVATE BOARD_HAS_PSRAM=1)
  endif()
  gtest_discover_tests(BookSearchRunner${board}Test)
endforeach()
