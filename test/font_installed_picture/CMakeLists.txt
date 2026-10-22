find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME FontInstalledPicture COMMAND "${Python3_EXECUTABLE}" -B
  "${CMAKE_CURRENT_SOURCE_DIR}/run.py" --repo "${REPO_ROOT}"
  --out "${CMAKE_CURRENT_BINARY_DIR}/out")
set_tests_properties(FontInstalledPicture PROPERTIES TIMEOUT 120)

# The name renderer on the real SdCardFont: a .cpfont built in memory (test/sd_card_font/stubs) of Latin letters.
add_executable(FontOwnNameTest
  FontOwnNameTest.cpp
  ${REPO_ROOT}/src/activities/settings/FontOwnName.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFont.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFontFamily.cpp
  ${REPO_ROOT}/lib/EpdFont/SdCardFont.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)
target_include_directories(FontOwnNameTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/test/sd_card_font/stubs
  ${REPO_ROOT}/src/activities/settings
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/Memory
)
target_link_libraries(FontOwnNameTest PRIVATE crosspoint_test_common GTest::gtest_main)
target_compile_options(FontOwnNameTest PRIVATE -fno-exceptions)
gtest_discover_tests(FontOwnNameTest)
