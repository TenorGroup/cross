cmake_minimum_required(VERSION 3.16)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(reader_build_budget_tests C CXX)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  enable_testing()
  include(GoogleTest)
  get_filename_component(REPO_ROOT "../.." ABSOLUTE)
  if(NOT GTEST_SOURCE)
    message(FATAL_ERROR "Set GTEST_SOURCE to a local googletest checkout")
  endif()
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
  add_subdirectory("${GTEST_SOURCE}" googletest EXCLUDE_FROM_ALL)
endif()
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME reader_build_budget_lifecycle
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run_tests.py"
          "${REPO_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/production-projection" --compiler "${CMAKE_CXX_COMPILER}")
add_test(NAME reader_build_budget_without_ble
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run_tests.py"
          "${REPO_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/production-projection-no-ble"
          --compiler "${CMAKE_CXX_COMPILER}" --ble-capability absent)
add_test(NAME reader_build_budget_ble_disabled_capability
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run_tests.py"
          "${REPO_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/production-projection-ble-zero"
          --compiler "${CMAKE_CXX_COMPILER}" --ble-capability 0)
set_tests_properties(reader_build_budget_lifecycle reader_build_budget_without_ble
  reader_build_budget_ble_disabled_capability PROPERTIES TIMEOUT 60)
add_executable(SectionBuildBudgetTest
  SectionBuildBudgetTest.cpp ../section_cache/ParserLinkStubs.cpp
  ${REPO_ROOT}/lib/Epub/Epub/Section.cpp
  ${REPO_ROOT}/lib/Epub/Epub/Page.cpp
  ${REPO_ROOT}/lib/Epub/Epub/blocks/TextBlock.cpp
  ${REPO_ROOT}/lib/MiniBidi/BidiUtils.cpp
  ${REPO_ROOT}/lib/MiniBidi/minibidi.c
  ${REPO_ROOT}/lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp
  ${REPO_ROOT}/lib/Epub/Epub/css/CssParser.cpp
  ${REPO_ROOT}/lib/Epub/Epub/ParsedText.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/ImageDimsProbe.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp)
target_include_directories(SectionBuildBudgetTest PRIVATE
  ../section_cache/stubs
  ${REPO_ROOT}/lib ${REPO_ROOT}/lib/Epub ${REPO_ROOT}/lib/Epub/Epub
  ${REPO_ROOT}/lib/Epub/Epub/parsers ${REPO_ROOT}/lib/Epub/Epub/css
  ${REPO_ROOT}/lib/EpdFont ${REPO_ROOT}/lib/Utf8 ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/Serialization ${REPO_ROOT}/lib/MiniBidi ${REPO_ROOT}/lib/FsHelpers
  ${REPO_ROOT}/lib/XmlParserUtils
  ${REPO_ROOT}/freeink-sdk/libs/hardware/SDCardManager/include)
target_link_libraries(SectionBuildBudgetTest PRIVATE GTest::gtest_main expat)
gtest_discover_tests(SectionBuildBudgetTest)
