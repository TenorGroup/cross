# Bo kiem LOI KHO SO LIEU DOC. Thuan, khong can gia lap phan cung nao.
add_executable(SoLieuDocTest SoLieuDocTest.cpp ${REPO_ROOT}/src/util/SoLieuDoc.cpp)
target_include_directories(SoLieuDocTest PRIVATE ${REPO_ROOT}/src)
target_link_libraries(SoLieuDocTest PRIVATE GTest::gtest_main)
gtest_discover_tests(SoLieuDocTest)
