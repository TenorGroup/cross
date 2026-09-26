add_executable(Bq27220CapacityTest
  Bq27220CapacityTest.cpp
  ${REPO_ROOT}/lib/hal/Bq27220Capacity.cpp
)
target_include_directories(Bq27220CapacityTest PRIVATE ${REPO_ROOT}/lib/hal)
target_link_libraries(Bq27220CapacityTest PRIVATE GTest::gtest_main)
target_compile_options(Bq27220CapacityTest PRIVATE
  -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer)
target_link_options(Bq27220CapacityTest PRIVATE -fsanitize=address,undefined)

gtest_discover_tests(Bq27220CapacityTest)
