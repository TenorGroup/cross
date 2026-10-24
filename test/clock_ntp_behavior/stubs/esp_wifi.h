#pragma once
// The Wi-Fi power-save query and switch the transfer guard uses; the host has no radio.
using esp_err_t = int;
using wifi_ps_type_t = int;
constexpr int ESP_OK = 0;
constexpr wifi_ps_type_t WIFI_PS_NONE = 0;
constexpr wifi_ps_type_t WIFI_PS_MIN_MODEM = 1;
inline esp_err_t esp_wifi_get_ps(wifi_ps_type_t* type) {
  *type = WIFI_PS_MIN_MODEM;
  return ESP_OK;
}
inline esp_err_t esp_wifi_set_ps(wifi_ps_type_t) { return ESP_OK; }
