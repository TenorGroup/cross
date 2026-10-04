# tenor/ugly: the pure decisions, the baked font, and the checks over generated files and strings.
add_executable(UglyShellTest UglyShellTest.cpp UglyTouchTest.cpp ${REPO_ROOT}/lib/Utf8/Utf8.cpp)
target_include_directories(UglyShellTest PRIVATE ${REPO_ROOT}/src ${REPO_ROOT}/lib/EpdFont ${REPO_ROOT}/lib/Utf8)
target_link_libraries(UglyShellTest PRIVATE GTest::gtest_main)
gtest_discover_tests(UglyShellTest)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME UglyShellAssets COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/check_assets.py --repo ${REPO_ROOT})
