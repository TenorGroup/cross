add_executable(FileSearchHelperTest FileSearchHelperTest.cpp
  ${REPO_ROOT}/src/components/FileSearch.cpp
  ${REPO_ROOT}/lib/LibraryIndex/LibraryText.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp)
target_include_directories(FileSearchHelperTest PRIVATE
  ${REPO_ROOT}/src/components ${REPO_ROOT}/lib ${REPO_ROOT}/lib/LibraryIndex
  ${REPO_ROOT}/lib/Utf8 ${REPO_ROOT}/lib/Memory)
add_test(NAME FileSearchHelperTest COMMAND FileSearchHelperTest)
add_executable(FileSearchScanTest FileSearchScanTest.cpp
  ${REPO_ROOT}/src/components/FileSearchScan.cpp
  ${REPO_ROOT}/src/components/FileSearch.cpp
  ${REPO_ROOT}/lib/LibraryIndex/LibraryText.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp)
target_include_directories(FileSearchScanTest PRIVATE
  ${REPO_ROOT}/src/components ${REPO_ROOT}/lib ${REPO_ROOT}/lib/LibraryIndex
  ${REPO_ROOT}/lib/Utf8 ${REPO_ROOT}/lib/Memory)
add_test(NAME FileSearchScanTest COMMAND FileSearchScanTest)
