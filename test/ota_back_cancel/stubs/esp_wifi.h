#pragma once
#include <cstdint>
enum wifi_ps_type_t { WIFI_PS_NONE, WIFI_PS_MIN_MODEM };
inline int esp_wifi_set_ps(wifi_ps_type_t) { return 0; }
struct wifi_ap_record_t {
  int8_t rssi;
};
inline int esp_wifi_sta_get_ap_info(wifi_ap_record_t* ap) {
  ap->rssi = -67;
  return 0;
}
