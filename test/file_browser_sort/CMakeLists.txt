add_executable(FileBrowserSortTest
  FileBrowserSortTest.cpp
  DocThuMucSortTest.cpp
  ${REPO_ROOT}/src/activities/home/FileBrowserSort.cpp
  ${REPO_ROOT}/src/activities/home/DocThuMuc.cpp
  ${REPO_ROOT}/lib/FsHelpers/FsHelpers.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

target_include_directories(FileBrowserSortTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/test/fs_helpers/stubs
  ${REPO_ROOT}/src/activities/home
  ${REPO_ROOT}/lib/FsHelpers
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/Utf8
)

target_link_libraries(FileBrowserSortTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(FileBrowserSortTest)

add_test(NAME FileBrowserSizeReadProduction
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/browser_search.py"
          --output "${CMAKE_CURRENT_BINARY_DIR}/browser-search")

add_test(NAME HomeFolderSortProduction
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/home_folder_sort.py"
          --home "${REPO_ROOT}/src/activities/home/HomeActivity.cpp"
          --ugly "${REPO_ROOT}/src/shells/ugly/UglyNotebook.cpp")
