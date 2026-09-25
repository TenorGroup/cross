#pragma once
#include <cstddef>
#include <cstdint>
// Records what the install did to the update slot and the boot selection.
using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0;
using esp_ota_handle_t = uint32_t;
struct esp_partition_t {
  size_t size;
};
struct OtaCalls {
  int begin = 0, abort = 0, end = 0, setBoot = 0;
  size_t written = 0;
};
extern OtaCalls otaCalls;
const esp_partition_t* esp_ota_get_next_update_partition(const esp_partition_t*);
esp_err_t esp_ota_begin(const esp_partition_t*, size_t, esp_ota_handle_t*);
esp_err_t esp_ota_write(esp_ota_handle_t, const void*, size_t);
esp_err_t esp_ota_abort(esp_ota_handle_t);
esp_err_t esp_ota_end(esp_ota_handle_t);
esp_err_t esp_ota_set_boot_partition(const esp_partition_t*);
inline const char* esp_err_to_name(esp_err_t) { return "error"; }
