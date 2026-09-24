# Edge latch between the button sample timer and the main loop. Pure logic.
add_executable(ButtonEdgeLatchTest ButtonEdgeLatchTest.cpp)
target_include_directories(ButtonEdgeLatchTest PRIVATE ${REPO_ROOT}/lib/hal)
target_compile_definitions(ButtonEdgeLatchTest PRIVATE REPO_ROOT_PATH="${REPO_ROOT}")
target_link_libraries(ButtonEdgeLatchTest PRIVATE GTest::gtest_main)
gtest_discover_tests(ButtonEdgeLatchTest)
