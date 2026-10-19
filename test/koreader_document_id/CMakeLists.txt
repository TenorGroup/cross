find_package(OpenSSL REQUIRED)
add_executable(KOReaderDocumentIdTest
  KOReaderDocumentIdTest.cpp
  ${REPO_ROOT}/lib/KOReaderSync/KOReaderDocumentId.cpp
)
target_include_directories(KOReaderDocumentIdTest BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stubs)
target_link_libraries(KOReaderDocumentIdTest PRIVATE crosspoint_test_common GTest::gtest_main OpenSSL::Crypto)
gtest_discover_tests(KOReaderDocumentIdTest)
