add_executable(IdleWorkTest IdleWorkTest.cpp)
target_include_directories(IdleWorkTest PRIVATE ${REPO_ROOT}/lib/IdleWork)
target_compile_options(IdleWorkTest PRIVATE -UNDEBUG -Werror)
add_test(NAME IdleWork COMMAND IdleWorkTest)
