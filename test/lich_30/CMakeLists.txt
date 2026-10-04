# Bo kiem LUOI 30 NGAY. Thuan, khong can gia lap phan cung nao.
add_executable(Lich30Test Lich30Test.cpp ${REPO_ROOT}/src/util/ReadingHabits.cpp ${REPO_ROOT}/src/util/NgayGio.cpp)
target_include_directories(Lich30Test PRIVATE ${REPO_ROOT}/src)
target_link_libraries(Lich30Test PRIVATE GTest::gtest_main)
gtest_discover_tests(Lich30Test)
