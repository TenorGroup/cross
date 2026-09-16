# Bo kiem NHA CUA DONG HANH DONG. Du lieu thuan, chi can I18n.
add_executable(SettingsTabsTest
  SettingsTabsTest.cpp
  ${REPO_ROOT}/src/activities/settings/SettingsTabs.cpp
  ${REPO_ROOT}/lib/I18n/I18n.cpp
  ${REPO_ROOT}/lib/I18n/I18nStrings.cpp
)

target_include_directories(SettingsTabsTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/I18n
)

target_link_libraries(SettingsTabsTest PRIVATE GTest::gtest_main)

gtest_discover_tests(SettingsTabsTest)
