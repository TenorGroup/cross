# Bo kiem lop anh xa nut. Bien dich NGUYEN VAN src/MappedInputManager.cpp,
# cong mot bo gia lap nen 92 dong trong stub/ va mot ban gia lap phan cung.
#
# Can .pio/libdeps/gh_release da co (chay `pio run -e gh_release` mot lan) vi
# CrossPointSettings.h dung thu vien JSON tai ve tu do.
set(PIO_LIBDEPS "${REPO_ROOT}/.pio/libdeps/gh_release")

add_executable(TabNavigationTest
  TabNavigationTest.cpp
  ${REPO_ROOT}/src/MenuFavorites.cpp
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

# stub/ phai dung TRUOC moi duong khac de che duoc header nang.
target_include_directories(TabNavigationTest BEFORE PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../host_stubs
)
# Quet toan bo thu muc nguon, giong het phep thu bang clang ngay 13/09 da chay sach.
file(GLOB_RECURSE _all_dirs LIST_DIRECTORIES true "${REPO_ROOT}/lib/*" "${REPO_ROOT}/freeink-sdk/*" "${REPO_ROOT}/src/*")
# Moi thu vien PlatformIO da tai (ArduinoJson, SdFat...), lay thu muc src cua tung cai.
file(GLOB _libdep_srcs LIST_DIRECTORIES true "${PIO_LIBDEPS}/*/src")
set(_inc_dirs "${REPO_ROOT}/src")
foreach(_d ${_libdep_srcs})
  if(IS_DIRECTORY ${_d})
    list(APPEND _inc_dirs ${_d})
  endif()
endforeach()
foreach(_d ${_all_dirs})
  # Bo qua thu muc kiem cua SDK: trong do co ban gia lap BoardConfig.h se che mat ban that.
  if(IS_DIRECTORY ${_d} AND NOT ${_d} MATCHES "pro_stubs" AND NOT ${_d} MATCHES "/test/host")
    list(APPEND _inc_dirs ${_d})
  endif()
endforeach()
list(REMOVE_DUPLICATES _inc_dirs)
target_include_directories(TabNavigationTest PRIVATE ${_inc_dirs})

target_compile_definitions(TabNavigationTest PRIVATE
  FREEINK_DEVICE_X3=1
  FREEINK_DEVICE_X4=1
  ENABLE_ARDUINO_FEATURES=0
  CROSSPOINT_VERSION="host-test"
)

target_link_libraries(TabNavigationTest PRIVATE
  GTest::gtest_main
)

# Exercise the real favorites catalog without linking unrelated activity constructors.
target_compile_options(TabNavigationTest PRIVATE -ffunction-sections -fdata-sections)
if(APPLE)
  target_link_options(TabNavigationTest PRIVATE -Wl,-dead_strip)
else()
  target_link_options(TabNavigationTest PRIVATE -Wl,--gc-sections)
endif()

gtest_discover_tests(TabNavigationTest)
