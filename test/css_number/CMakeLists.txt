# The expected floats come from exact rational arithmetic in gen_vectors.py, not from a C library.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(CSS_NUMBER_VECTORS ${CMAKE_CURRENT_BINARY_DIR}/CssNumberVectors.inc)
add_custom_command(
  OUTPUT ${CSS_NUMBER_VECTORS}
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/gen_vectors.py ${CSS_NUMBER_VECTORS} 16
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/gen_vectors.py
)
add_executable(CssNumberTest
  CssNumberTest.cpp
  ${CSS_NUMBER_VECTORS}
  ${REPO_ROOT}/lib/Epub/Epub/css/CssParser.cpp
)
target_include_directories(CssNumberTest PRIVATE
  ${CMAKE_CURRENT_BINARY_DIR}
  ${REPO_ROOT}/test/css_parser/stubs
  ${REPO_ROOT}/lib/Epub/Epub/css
  ${REPO_ROOT}/lib/Memory
)
target_link_libraries(CssNumberTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(CssNumberTest)
