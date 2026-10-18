add_executable(DictionaryLookupPolicyTest
  DictionaryLookupPolicyTest.cpp
  ${REPO_ROOT}/src/util/DictionaryLookupPolicy.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

target_include_directories(DictionaryLookupPolicyTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/Utf8
)

target_link_libraries(DictionaryLookupPolicyTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(DictionaryLookupPolicyTest)
