add_executable(SecureHttpTransportTest TransportTest.cpp)
target_include_directories(SecureHttpTransportTest BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stubs ${REPO_ROOT}/freeink-sdk/libs/network/SecureNet/include)
target_link_libraries(SecureHttpTransportTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(SecureHttpTransportTest)

# The headless OPDS login (SDK) over the same stubbed TLS wire.
set(OPDS_SRC ${REPO_ROOT}/freeink-sdk/libs/network/Opds)
add_executable(OpdsImplicitAuthTest
  OpdsImplicitAuthTest.cpp
  ${OPDS_SRC}/src/OpdsImplicitAuth.cpp
  ${OPDS_SRC}/src/OpdsLoginForm.cpp
  ${OPDS_SRC}/src/OpdsSearchTemplate.cpp
)
target_compile_definitions(OpdsImplicitAuthTest PRIVATE FREEINK_NET_WOLFSSL=1 CROSSPOINT_VERSION="test")
target_include_directories(OpdsImplicitAuthTest BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stubs ${REPO_ROOT}/freeink-sdk/libs/network/SecureNet/include ${OPDS_SRC}/include)
target_link_libraries(OpdsImplicitAuthTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(OpdsImplicitAuthTest)
