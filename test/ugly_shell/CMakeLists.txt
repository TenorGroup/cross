# tenor/ugly: the pure decisions, the baked font, and the checks over generated files and strings.
add_executable(UglyShellTest UglyShellTest.cpp UglyTouchTest.cpp ${REPO_ROOT}/lib/Utf8/Utf8.cpp)
target_include_directories(UglyShellTest PRIVATE ${REPO_ROOT}/src ${REPO_ROOT}/lib/EpdFont ${REPO_ROOT}/lib/Utf8)
target_link_libraries(UglyShellTest PRIVATE GTest::gtest_main)
gtest_discover_tests(UglyShellTest)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME UglyRootNavigation COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/root_navigation.py
  --repo ${REPO_ROOT} --output ${CMAKE_CURRENT_BINARY_DIR}/root-navigation)
add_test(NAME UglyRecentActions COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/recent_actions.py --repo ${REPO_ROOT})
add_test(NAME UglyShellAssets COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/check_assets.py --repo ${REPO_ROOT})
add_test(NAME UglyBatteryPercent COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/battery_percent.py
  --repo ${REPO_ROOT} --output ${CMAKE_CURRENT_BINARY_DIR}/battery-percent)
add_test(NAME UglyIconRoutes COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/icon_routes.py --repo ${REPO_ROOT})
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${REPO_ROOT}/src/shells/ugly/UglyInk.cpp)
execute_process(COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/icon_pen.py
  ${REPO_ROOT}/src/shells/ugly/UglyInk.cpp ${CMAKE_CURRENT_BINARY_DIR}/icon_pen.cpp
  COMMAND_ERROR_IS_FATAL ANY)
add_executable(UglyIconTest IconTest.cpp ${CMAKE_CURRENT_BINARY_DIR}/icon_pen.cpp
  ${REPO_ROOT}/src/shells/ugly/UglyIconInk.cpp ${REPO_ROOT}/src/shells/ugly/UglyIconAssets.cpp)
target_include_directories(UglyIconTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/icon_stubs ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/Utf8 ${REPO_ROOT}/lib/GfxRenderer ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/freeink-sdk/libs/assets/Icons/include)
target_link_libraries(UglyIconTest PRIVATE GTest::gtest_main)
gtest_discover_tests(UglyIconTest)
if(APPLE)
  add_library(UglyIconHeapProbe SHARED IconHeapProbe.c)
  add_test(NAME UglyIconLibcHeap COMMAND ${CMAKE_COMMAND} -E env
    DYLD_INSERT_LIBRARIES=$<TARGET_FILE:UglyIconHeapProbe> UGLY_REQUIRE_LIBC_HEAP_PROBE=1
    $<TARGET_FILE:UglyIconTest> --gtest_filter=UglyIcons.RasterIsDeterministicVariedBoundedAndHeapFree)
endif()
