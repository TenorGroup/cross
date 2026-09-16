add_executable(SecureHttpTransportTest TransportTest.cpp)
target_include_directories(SecureHttpTransportTest BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stubs ${REPO_ROOT}/freeink-sdk/libs/network/SecureNet/include)
target_link_libraries(SecureHttpTransportTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(SecureHttpTransportTest)
