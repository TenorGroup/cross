add_executable(ZipCentralDirTest
  ZipCentralDirTest.cpp
  ${REPO_ROOT}/lib/ZipFile/ZipFile.cpp
  ${REPO_ROOT}/lib/miniz/src/InflateStream.cpp
  ${REPO_ROOT}/lib/miniz/src/miniz_impl.c
  ${REPO_ROOT}/lib/Memory/BuildScratch.cpp
  ${REPO_ROOT}/lib/hal/HalMemory.cpp
)
target_include_directories(ZipCentralDirTest PRIVATE
  stubs
  ${REPO_ROOT}/test/host_stubs
  ${REPO_ROOT}/test/inflate_stream/stubs
  ${REPO_ROOT}/lib/ZipFile
  ${REPO_ROOT}/lib/miniz/src
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/hal
)
target_link_libraries(ZipCentralDirTest PRIVATE GTest::gtest_main)
gtest_discover_tests(ZipCentralDirTest)
