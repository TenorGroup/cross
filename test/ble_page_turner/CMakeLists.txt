# BTH2: bo kiem duong page-turner BLE bang dau vao GIA.
#
# Bien dich NGUYEN VAN ma san xuat, khong chep lai:
#   freeink-sdk/libs/network/BleKeyboardHost/src/BleKeyboardHost.cpp
#   freeink-sdk/libs/network/BleKeyboardHost/src/HidKeymap.cpp
# voi FREEINK_CAP_BLE_HID_HOST=1 (lay tu .../tests/stubs/BoardConfig.h) va dieu
# khien bang fake NimBLE cua chinh thu vien (.../tests/FakeBle.cpp): khung report
# HID bom vao di qua bo giai ma that. Them src/CrossPointSettings.h, vi
# blePageActionFor() chinh la ham main.cpp goi cho tung su kien lay ra tu hang doi.
#
# Can .pio/libdeps/gh_release cho <ArduinoJson.h> ma CrossPointSettings.h keo vao;
# thieu thi chi bo kiem nay khong build duoc, cac bo kiem khac khong bi anh huong.

set(BLE_HID_DIR ${REPO_ROOT}/freeink-sdk/libs/network/BleKeyboardHost)
set(PIO_LIBDEPS "${REPO_ROOT}/.pio/libdeps/gh_release")
set(ARDUINOJSON_SRC "${PIO_LIBDEPS}/ArduinoJson/src")
if(NOT IS_DIRECTORY "${ARDUINOJSON_SRC}")
  message(WARNING
    "BlePageTurnerFakeInputTest needs <ArduinoJson.h> from ${ARDUINOJSON_SRC}; run `pio run -e gh_release` once.")
endif()

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
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Serialization
)

# Moi thu vien PlatformIO da tai (ArduinoJson...), lay thu muc src cua tung cai.
file(GLOB _libdep_srcs LIST_DIRECTORIES true "${PIO_LIBDEPS}/*/src")
target_include_directories(BlePageTurnerFakeInputTest PRIVATE ${_libdep_srcs})

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
