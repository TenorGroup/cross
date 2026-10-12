#pragma once
// The two app slots of the X4 Pro as its flash really holds them (0x7e0000 each, log of 11/10/2026),
// larger than partitions.csv says: nothing may assume the slot size.
#include <cstddef>
#include <cstdint>
using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0;
struct esp_partition_t {
  const char* label;
  uint32_t address;
  uint32_t size;
};
esp_err_t esp_partition_read(const esp_partition_t*, size_t offset, void* dst, size_t size);
esp_err_t esp_partition_erase_range(const esp_partition_t*, size_t offset, size_t size);
esp_err_t esp_partition_write(const esp_partition_t*, size_t offset, const void* src, size_t size);
