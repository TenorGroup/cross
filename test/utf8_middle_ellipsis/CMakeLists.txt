add_executable(Utf8MiddleEllipsisTest
  Utf8MiddleEllipsisTest.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp)
target_link_libraries(Utf8MiddleEllipsisTest PRIVATE crosspoint_test_common)
target_include_directories(Utf8MiddleEllipsisTest PRIVATE ${REPO_ROOT}/lib/Utf8)
add_test(NAME Utf8MiddleEllipsis COMMAND Utf8MiddleEllipsisTest)
