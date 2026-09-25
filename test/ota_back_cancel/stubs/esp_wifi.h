#pragma once
enum wifi_ps_type_t { WIFI_PS_NONE, WIFI_PS_MIN_MODEM };
inline int esp_wifi_set_ps(wifi_ps_type_t) { return 0; }
