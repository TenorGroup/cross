set(PIO_LIBDEPS "${REPO_ROOT}/.pio/libdeps/gh_release")
set(ARDUINOJSON_SRC "${PIO_LIBDEPS}/ArduinoJson/src")
add_executable(RecentExcerptTest RecentExcerptTest.cpp ${REPO_ROOT}/src/RecentBooksStore.cpp)
# The stubs come first: they stand in for the store base, the book loaders and the card.
target_include_directories(RecentExcerptTest BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stubs)
target_include_directories(RecentExcerptTest PRIVATE ${ARDUINOJSON_SRC} ${REPO_ROOT}/src)
target_link_libraries(RecentExcerptTest PRIVATE GTest::gtest_main)
gtest_discover_tests(RecentExcerptTest)
