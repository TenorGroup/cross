#pragma once
constexpr int WIFI_MODE_NULL = 0;
struct WiFiStub { int getMode() const { return WIFI_MODE_NULL; } };
inline WiFiStub WiFi;
