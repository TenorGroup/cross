# BTH2: bo kiem duong page-turner BLE bang dau vao GIA.
#
# Bien dich NGUYEN VAN ma san xuat, khong chep lai:
#   freeink-sdk/libs/network/BleKeyboardHost/src/BleKeyboardHost.cpp
#   freeink-sdk/libs/network/BleKeyboardHost/src/HidKeymap.cpp
# voi FREEINK_CAP_BLE_HID_HOST=1 (lay tu .../tests/stubs/BoardConfig.h) va dieu
# khien bang fake NimBLE cua chinh thu vien (.../tests/FakeBle.cpp): khung report
# HID bom vao di qua bo giai ma that. Anh xa phim va bang nut la cua
# lib/BlePageTurner (BlePageTurner.h, BleKeyBinding.h), dung ham module goi cho tung
# su kien lay ra tu hang doi. Cac bai kiem thuan cua module nam o lib/BlePageTurner/test.

set(BLE_HID_DIR ${REPO_ROOT}/freeink-sdk/libs/network/BleKeyboardHost)

add_executable(BlePageTurnerFakeInputTest
  BlePageTurnerFakeInputTest.cpp
  ${BLE_HID_DIR}/tests/FakeBle.cpp
  ${BLE_HID_DIR}/src/HidKeymap.cpp
  ${BLE_HID_DIR}/src/BleKeyboardHost.cpp
)

# stubs/ phai dung TRUOC moi duong khac: Logging.h o day thay cho ban that (ban
# that keo Arduino/HardwareSerial/Print vao, khong lien quan phep kiem nay).
target_include_directories(BlePageTurnerFakeInputTest BEFORE PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
)

# Thu tu sau day la co y: stub cua thu vien BLE phai thang BoardConfig.h cua SDK,
# neu khong BleKeyboardHost.cpp se build nhanh "khong co BLE" va khong co su kien nao.
target_include_directories(BlePageTurnerFakeInputTest PRIVATE
  ${BLE_HID_DIR}/tests/stubs
  ${BLE_HID_DIR}/tests
  ${BLE_HID_DIR}/include
  ${BLE_HID_DIR}/src
  ${REPO_ROOT}/lib/BlePageTurner/include
)

target_compile_definitions(BlePageTurnerFakeInputTest PRIVATE
  FREEINK_DEVICE_X3=1
  FREEINK_DEVICE_X4=1
  ENABLE_ARDUINO_FEATURES=0
  CROSSPOINT_VERSION="host-test"
)

target_link_libraries(BlePageTurnerFakeInputTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(BlePageTurnerFakeInputTest)
