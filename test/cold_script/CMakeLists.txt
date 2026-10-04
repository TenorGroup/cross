add_executable(ColdScriptTest ColdScriptTest.cpp)
target_include_directories(ColdScriptTest PRIVATE ${REPO_ROOT}/src ${REPO_ROOT}/freeink-sdk/libs/hardware/SDCardManager/include)
target_link_libraries(ColdScriptTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(ColdScriptTest)
