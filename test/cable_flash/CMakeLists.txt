# The X4 Pro cable flash channel: the rule that keeps it in every image (card, network, cable) and
# the cable session itself, built as the X4 Pro release builds them.
set(CABLE_FLASH_DEFS FREEINK_DEVICE_X4PRO=1 TENOR_CABLE_FLASH=1 CROSSPOINT_VERSION="v1.0.59")
set(CABLE_FLASH_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/stubs ${REPO_ROOT}/src
    ${REPO_ROOT}/test/file_transfer_back_latch/stubs
    ${REPO_ROOT}/freeink-sdk/libs/hardware/SDCardManager/include)

add_executable(CableFlashPolicyTest PolicyTest.cpp ${REPO_ROOT}/src/network/FirmwareBoardTag.cpp
               ${REPO_ROOT}/src/network/FirmwareFlasher.cpp)
target_compile_definitions(CableFlashPolicyTest PRIVATE ${CABLE_FLASH_DEFS})
target_include_directories(CableFlashPolicyTest PRIVATE ${CABLE_FLASH_INCLUDES})
target_link_libraries(CableFlashPolicyTest PRIVATE GTest::gtest_main)
if(NOT APPLE)
  target_link_libraries(CableFlashPolicyTest PRIVATE crypto)
endif()
gtest_discover_tests(CableFlashPolicyTest)

add_executable(CableFlashSessionTest SessionTest.cpp ${REPO_ROOT}/src/network/FirmwareBoardTag.cpp)
target_compile_definitions(CableFlashSessionTest PRIVATE ${CABLE_FLASH_DEFS})
target_include_directories(CableFlashSessionTest PRIVATE ${CABLE_FLASH_INCLUDES}
                           ${REPO_ROOT}/freeink-sdk/libs/hardware/SDCardManager/include)
target_compile_options(CableFlashSessionTest PRIVATE -Wall -Wextra)
target_link_libraries(CableFlashSessionTest PRIVATE GTest::gtest_main)
if(NOT APPLE)
  target_link_libraries(CableFlashSessionTest PRIVATE crypto)
endif()
gtest_discover_tests(CableFlashSessionTest)

# tools/nap.py, the computer's side, against fake ports, a fake X4 Pro and a fake esptool.
add_test(NAME CableFlashTool COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/test/cable_tool/test_nap.py)
