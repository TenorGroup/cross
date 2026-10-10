add_executable(ConfirmationLayoutTest
  ConfirmationTextTest.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ConfirmationLayoutTest.cpp
  ConfirmationProductionContractTest.cpp
)

target_include_directories(ConfirmationLayoutTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/Utf8
)
target_compile_definitions(ConfirmationLayoutTest PRIVATE
  REPO_ROOT_PATH="${REPO_ROOT}"
)
target_link_libraries(ConfirmationLayoutTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ConfirmationLayoutTest)
