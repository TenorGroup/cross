add_executable(FontStoreNamesTest FontStoreNamesTest.cpp)

target_include_directories(FontStoreNamesTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/include
)
target_link_libraries(FontStoreNamesTest PRIVATE
  crosspoint_test_common
)

add_test(NAME FontStoreNamesTest COMMAND FontStoreNamesTest)
