add_executable(ThumbNameTest ThumbNameTest.cpp)
target_include_directories(ThumbNameTest PRIVATE ${REPO_ROOT}/src)
target_link_libraries(ThumbNameTest PRIVATE GTest::gtest_main)
gtest_discover_tests(ThumbNameTest)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME ThumbCacheReuse COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_cache_reuse.py)
