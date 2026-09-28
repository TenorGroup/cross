add_executable(BattShownTest
  BattShownTest.cpp
  ${REPO_ROOT}/lib/hal/BattShown.cpp
)
target_include_directories(BattShownTest PRIVATE ${REPO_ROOT}/lib/hal)
target_link_libraries(BattShownTest PRIVATE GTest::gtest_main)
target_compile_options(BattShownTest PRIVATE
  -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer)
target_link_options(BattShownTest PRIVATE -fsanitize=address,undefined)

gtest_discover_tests(BattShownTest)
