# The X4 Pro cable flash channel: the rule that keeps it in every image (card, network, cable) and
# the cable session itself, built as the X4 Pro release builds them.
set(CABLE_FLASH_DEFS FREEINK_DEVICE_X4PRO=1 TENOR_CABLE_FLASH=1 CROSSPOINT_VERSION="v1.0.59")
set(CABLE_FLASH_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/stubs ${REPO_ROOT}/src
    ${REPO_ROOT}/test/file_transfer_back_latch/stubs)

add_executable(CableFlashPolicyTest PolicyTest.cpp ${REPO_ROOT}/src/network/FirmwareBoardTag.cpp
               ${REPO_ROOT}/src/network/FirmwareFlasher.cpp)
target_compile_definitions(CableFlashPolicyTest PRIVATE ${CABLE_FLASH_DEFS})
target_include_directories(CableFlashPolicyTest PRIVATE ${CABLE_FLASH_INCLUDES})
target_link_libraries(CableFlashPolicyTest PRIVATE GTest::gtest_main)
if(NOT APPLE)
  target_link_libraries(CableFlashPolicyTest PRIVATE crypto)
endif()
gtest_discover_tests(CableFlashPolicyTest)
