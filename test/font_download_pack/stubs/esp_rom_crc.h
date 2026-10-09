#pragma once
#include <cstdint>
inline uint32_t esp_rom_crc32_le(uint32_t crc, const uint8_t* bytes, uint32_t size) {
  crc = ~crc;
  for (uint32_t index = 0; index < size; ++index) {
    crc ^= bytes[index];
    for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0u);
  }
  return ~crc;
}
