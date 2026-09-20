#pragma once

constexpr int WL_CONNECTED = 3;

struct WiFiClass {
  int status() const;
};

extern WiFiClass WiFi;
