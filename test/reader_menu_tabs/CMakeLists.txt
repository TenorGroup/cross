# Bo kiem CACH CHIA TAB cua menu luc dang doc.
#
# Chi bien dich ReaderMenuLayout.cpp va I18n. Phan chia tab la du lieu thuan nen
# no khong keo theo the nho, cai dat hay man hinh; nho vay bo kiem nay nhe va
# khong can ban gia lap phan cung nao.
add_executable(ReaderMenuTabsTest
  ReaderMenuTabsTest.cpp
  ${REPO_ROOT}/src/activities/reader/ReaderMenuLayout.cpp
  ${REPO_ROOT}/lib/I18n/I18n.cpp
  ${REPO_ROOT}/lib/I18n/I18nStrings.cpp
)

target_include_directories(ReaderMenuTabsTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/I18n
)

target_link_libraries(ReaderMenuTabsTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ReaderMenuTabsTest)
