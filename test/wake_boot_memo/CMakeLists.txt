# What a deep-sleep wake may reuse from the boot before it instead of measuring again:
# the panel controller the probe found and the SD reader font family the last boot loaded.
add_executable(WakeBootMemoTest PanelMemoTest.cpp)
target_include_directories(WakeBootMemoTest PRIVATE ${REPO_ROOT}/lib/hal ${REPO_ROOT}/src)
target_link_libraries(WakeBootMemoTest PRIVATE GTest::gtest_main)
gtest_discover_tests(WakeBootMemoTest)

# The production font system, compiled from a copy so its quoted includes of the settings and
# the manager land on the host stubs above; the registry walk is counted, not run.
configure_file(${REPO_ROOT}/src/SdCardFontSystem.cpp ${CMAKE_CURRENT_BINARY_DIR}/SdCardFontSystem.cpp COPYONLY)
add_executable(SdFontBootTest SdFontBootTest.cpp HostRegistry.cpp ${CMAKE_CURRENT_BINARY_DIR}/SdCardFontSystem.cpp)
target_include_directories(SdFontBootTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/GfxRenderer
  ${REPO_ROOT}/lib/Memory
)
target_link_libraries(SdFontBootTest PRIVATE GTest::gtest_main)
gtest_discover_tests(SdFontBootTest)

# The same, as a board with PSRAM builds it: a size the heap cannot hold is refused, not dropped.
add_executable(SdFontBootPsramTest SdFontBootTest.cpp HostRegistry.cpp ${CMAKE_CURRENT_BINARY_DIR}/SdCardFontSystem.cpp)
target_compile_definitions(SdFontBootPsramTest PRIVATE FREEINK_SIZES_NEED_ROOM)
target_include_directories(SdFontBootPsramTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/GfxRenderer
  ${REPO_ROOT}/lib/Memory
)
target_link_libraries(SdFontBootPsramTest PRIVATE GTest::gtest_main)
gtest_discover_tests(SdFontBootPsramTest TEST_SUFFIX .Psram)
