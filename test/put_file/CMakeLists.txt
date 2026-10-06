# CMD:PUT: the command line, the sum and the receive loop that write a file sent over the cable.
add_executable(PutFileTest PutFileTest.cpp)
target_include_directories(PutFileTest PRIVATE ${REPO_ROOT}/src ${REPO_ROOT}/freeink-sdk/libs/hardware/SDCardManager/include)
target_link_libraries(PutFileTest PRIVATE GTest::gtest_main)
gtest_discover_tests(PutFileTest)
