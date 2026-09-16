# Bo kiem PHEP LICH. Thuan, khong can gia lap phan cung nao.
add_executable(NgayGioTest NgayGioTest.cpp ${REPO_ROOT}/src/util/NgayGio.cpp)
target_include_directories(NgayGioTest PRIVATE ${REPO_ROOT}/src)
target_link_libraries(NgayGioTest PRIVATE GTest::gtest_main)
gtest_discover_tests(NgayGioTest)
