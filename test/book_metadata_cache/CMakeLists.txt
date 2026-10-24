# Upstream's checks of the size windows, on the real files of the host (chapter_html_slim_parser stubs).
add_executable(BookMetadataCacheTest
  BookMetadataCacheTest.cpp
  ${REPO_ROOT}/lib/Epub/Epub/BookMetadataCache.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

target_include_directories(BookMetadataCacheTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs_file
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/test/chapter_html_slim_parser/stubs
)

target_link_libraries(BookMetadataCacheTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(BookMetadataCacheTest)

# The fault checks of the book index (short reads, failed allocations, failed writes) on an in-memory card.
add_executable(BookMetadataCacheFaultsTest
  BookMetadataCacheFaultsTest.cpp
  ${REPO_ROOT}/lib/Epub/Epub/BookMetadataCache.cpp
)
target_include_directories(BookMetadataCacheFaultsTest PRIVATE
  stubs_mem
  ${REPO_ROOT}/lib/Epub/Epub
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/Memory
)
add_test(NAME BookMetadataCacheFaults COMMAND BookMetadataCacheFaultsTest)
