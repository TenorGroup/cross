# ProgressFile::writeAtomic and openForRead against a card that dies part way through a save.
add_executable(ProgressFileTest ProgressFileTest.cpp)
target_include_directories(ProgressFileTest BEFORE PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/freeink-sdk/libs/hardware/SDCardManager/include
)
target_link_libraries(ProgressFileTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(ProgressFileTest)
