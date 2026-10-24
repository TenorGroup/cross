#pragma once
// Power-save control the transport guard talks to; the fixture records the last mode set.
using esp_err_t = int;
using wifi_ps_type_t = int;
constexpr int ESP_OK = 0, WIFI_PS_NONE = 0, WIFI_PS_MIN_MODEM = 1;
inline wifi_ps_type_t g_wifiPs = WIFI_PS_MIN_MODEM;
inline esp_err_t esp_wifi_get_ps(wifi_ps_type_t* type) {
  *type = g_wifiPs;
  return ESP_OK;
}
inline esp_err_t esp_wifi_set_ps(wifi_ps_type_t type) {
  g_wifiPs = type;
  return ESP_OK;
}
