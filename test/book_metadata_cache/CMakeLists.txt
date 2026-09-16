add_executable(BookMetadataCacheTest
  BookMetadataCacheTest.cpp
  ${REPO_ROOT}/lib/Epub/Epub/BookMetadataCache.cpp
)
target_include_directories(BookMetadataCacheTest PRIVATE
  stubs
  ${REPO_ROOT}/lib/Epub/Epub
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/Memory
)
add_test(NAME BookMetadataCacheFaults COMMAND BookMetadataCacheTest)
