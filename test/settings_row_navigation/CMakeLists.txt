cmake_minimum_required(VERSION 3.16)
project(SettingsRowNavigationTest LANGUAGES CXX)

enable_testing()
find_package(Python3 COMPONENTS Interpreter REQUIRED)
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

add_test(
  NAME SettingsRowNavigation
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run.py
          --repo ${REPO_ROOT}
          --output ${CMAKE_CURRENT_BINARY_DIR}/harness
          --cxx ${CMAKE_CXX_COMPILER}
)
