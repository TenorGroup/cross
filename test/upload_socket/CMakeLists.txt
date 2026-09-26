# Upload socket shared by the web server's main task and the Back sampler task.
find_package(Threads REQUIRED)
add_executable(UploadSocketTest UploadSocketTest.cpp)
target_include_directories(UploadSocketTest PRIVATE ${REPO_ROOT}/src/network)
target_compile_definitions(UploadSocketTest PRIVATE REPO_ROOT_PATH="${REPO_ROOT}")
target_link_libraries(UploadSocketTest PRIVATE GTest::gtest_main Threads::Threads)
gtest_discover_tests(UploadSocketTest)
