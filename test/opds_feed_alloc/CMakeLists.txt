set(OPDS ${REPO_ROOT}/freeink-sdk/libs/network/Opds)
add_executable(OpdsFeedAllocTest
  OpdsFeedAllocTest.cpp
  ${OPDS}/src/OpdsFeedParser.cpp
  ${OPDS}/src/Opds2Parser.cpp
  ${OPDS}/src/OpdsParser.cpp
  ${REPO_ROOT}/freeink-sdk/libs/network/JsonSax/src/StreamingJsonParser.cpp
)
target_include_directories(OpdsFeedAllocTest PRIVATE
  ${REPO_ROOT}/test/host_stubs
  ${OPDS}/include
  ${REPO_ROOT}/freeink-sdk/libs/network/JsonSax/include
  ${REPO_ROOT}/lib/JsonParser
)
target_link_libraries(OpdsFeedAllocTest PRIVATE crosspoint_test_common GTest::gtest_main expat)
gtest_discover_tests(OpdsFeedAllocTest)
