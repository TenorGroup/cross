# Bo kiem THANH THE CHAY. Cung khuon voi tab_navigation: bien dich nguyen van lop tab
# that, cong bo gia lap nen o host_stubs/.
set(PIO_LIBDEPS "${REPO_ROOT}/.pio/libdeps/gh_release")

add_executable(TabWindowTest
  TabWindowTest.cpp
  ${REPO_ROOT}/test/mapped_input/FakeHal.cpp
  ${REPO_ROOT}/test/host_stubs/FakeUi.cpp
  ${REPO_ROOT}/src/MappedInputManager.cpp
  ${REPO_ROOT}/src/activities/Activity.cpp
  ${REPO_ROOT}/src/activities/UiListActivity.cpp
  ${REPO_ROOT}/src/activities/UiTabListActivity.cpp
  ${REPO_ROOT}/src/util/ButtonNavigator.cpp
  ${REPO_ROOT}/src/components/UiAppHost.cpp
  ${REPO_ROOT}/lib/I18n/I18n.cpp
  ${REPO_ROOT}/lib/I18n/I18nStrings.cpp
  ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/src/FreeInkUI.cpp
)

target_include_directories(TabWindowTest BEFORE PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../host_stubs
)
file(GLOB_RECURSE _all_dirs LIST_DIRECTORIES true "${REPO_ROOT}/lib/*" "${REPO_ROOT}/freeink-sdk/*" "${REPO_ROOT}/src/*")
file(GLOB _libdep_srcs LIST_DIRECTORIES true "${PIO_LIBDEPS}/*/src")
set(_inc_dirs "${REPO_ROOT}/src")
foreach(_d ${_libdep_srcs})
  if(IS_DIRECTORY ${_d})
    list(APPEND _inc_dirs ${_d})
  endif()
endforeach()
foreach(_d ${_all_dirs})
  if(IS_DIRECTORY ${_d} AND NOT ${_d} MATCHES "pro_stubs" AND NOT ${_d} MATCHES "/test/host")
    list(APPEND _inc_dirs ${_d})
  endif()
endforeach()
list(REMOVE_DUPLICATES _inc_dirs)
target_include_directories(TabWindowTest PRIVATE ${_inc_dirs})

target_compile_definitions(TabWindowTest PRIVATE
  FREEINK_DEVICE_X3=1
  FREEINK_DEVICE_X4=1
  ENABLE_ARDUINO_FEATURES=0
  CROSSPOINT_VERSION="host-test"
)

target_link_libraries(TabWindowTest PRIVATE
  GTest::gtest_main
)

gtest_discover_tests(TabWindowTest)
