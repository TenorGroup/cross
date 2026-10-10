add_executable(ConfirmationLayoutTest
  ConfirmationLayoutTest.cpp
  ConfirmationProductionContractTest.cpp
)

target_include_directories(ConfirmationLayoutTest PRIVATE
  ${REPO_ROOT}/src
)
target_compile_definitions(ConfirmationLayoutTest PRIVATE
  REPO_ROOT_PATH="${REPO_ROOT}"
)
target_link_libraries(ConfirmationLayoutTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ConfirmationLayoutTest)
