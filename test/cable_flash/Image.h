#pragma once
// An ESP app image as esptool writes it: header, one segment holding the product tag, the XOR
// checksum at the 16 byte boundary and the SHA-256 of all that appended (test/ota_package fixture).
#include <mbedtls/sha256.h>

#include <cstdint>
#include <string>
#include <vector>

inline std::vector<uint8_t> makeImage(const std::string& tag, uint16_t chip = 9, size_t body = 70000) {
  std::vector<uint8_t> data(24, 0);
  data[0] = 0xE9;
  data[1] = 1;
  data[12] = chip & 0xFF;
  data[13] = chip >> 8;
  data[23] = 1;
  std::vector<uint8_t> segment(body);
  for (size_t i = 0; i < body; ++i) segment[i] = static_cast<uint8_t>(i * 7 + 3) | 0x80;
  for (size_t i = 0; i <= tag.size(); ++i) segment[1000 + i] = i < tag.size() ? tag[i] : 0;
  const uint32_t load = 0x3C000020, len = static_cast<uint32_t>(body);
  for (int i = 0; i < 4; ++i) data.push_back((load >> (8 * i)) & 0xFF);
  for (int i = 0; i < 4; ++i) data.push_back((len >> (8 * i)) & 0xFF);
  uint8_t sum = 0xEF;
  for (uint8_t b : segment) sum ^= b;
  data.insert(data.end(), segment.begin(), segment.end());
  while (data.size() % 16 != 15) data.push_back(0);
  data.push_back(sum);
  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts(&sha, 0);
  mbedtls_sha256_update(&sha, data.data(), data.size());
  uint8_t digest[32];
  mbedtls_sha256_finish(&sha, digest);
  data.insert(data.end(), digest, digest + 32);
  return data;
}

// Tags as the images carry them: this build's, the v1.0.58 X4 Pro release's (no cable channel), and
// an X4 Classic build's.
inline const std::string kThisTag = "CROSSPOINT-BOARD-V1:x4pro;TENOR-CROSS-VERSION-V1:v1.0.59;TENOR-CABLE-V1:1;";
inline const std::string k58Tag = "CROSSPOINT-BOARD-V1:x4pro;TENOR-CROSS-VERSION-V1:v1.0.58;";
inline const std::string kX4cTag = "CROSSPOINT-BOARD-V1:x4c;TENOR-CROSS-VERSION-V1:v1.0.59;";
