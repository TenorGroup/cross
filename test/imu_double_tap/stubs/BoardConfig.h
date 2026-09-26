#pragma once

#include <cstdint>

// The X3's sensor bus as the SDK driver reads it: QMI8658 preferred at 0x6B.
#define FREEINK_CAP_IMU 1

namespace BoardConfig {
enum class RtcType : uint8_t { None, Pcf8563, Ds3231 };
enum class ImuType : uint8_t { None, Lsm6ds3, Qmi8658 };
struct SensorsConfig {
  int8_t i2cSda;
  int8_t i2cScl;
  uint32_t i2cHz;
  uint8_t rtcAddr;
  uint8_t tempHumidityAddr;
  uint8_t imuAddr;
  uint8_t i2cBus = 0;
  RtcType rtcType = RtcType::None;
  ImuType imuType = ImuType::None;
};
struct Board {
  SensorsConfig sensors;
};
inline Board ACTIVE{{20, 0, 400000, 0x68, 0, 0x6B, 0, RtcType::Ds3231, ImuType::Qmi8658}};
}  // namespace BoardConfig
