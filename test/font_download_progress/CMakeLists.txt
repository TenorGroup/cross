add_executable(FontDownloadProgressTest ProgressGateTest.cpp)

target_include_directories(FontDownloadProgressTest PRIVATE
  ${REPO_ROOT}/src
)
target_compile_definitions(FontDownloadProgressTest PRIVATE
  REPO_ROOT_PATH="${REPO_ROOT}"
)
target_link_libraries(FontDownloadProgressTest PRIVATE
  crosspoint_test_common
)

add_test(NAME FontDownloadProgressTest COMMAND FontDownloadProgressTest)
