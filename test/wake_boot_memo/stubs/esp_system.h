#pragma once
// The reset reason the boot under test sees; the test sets it before each begin().
enum esp_reset_reason_t { ESP_RST_POWERON = 1, ESP_RST_SW = 3, ESP_RST_DEEPSLEEP = 8 };
inline esp_reset_reason_t hostResetReason = ESP_RST_POWERON;
inline esp_reset_reason_t esp_reset_reason() { return hostResetReason; }
