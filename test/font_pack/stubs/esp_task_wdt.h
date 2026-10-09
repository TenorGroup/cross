#pragma once
#define ESP_OK 0
inline int esp_task_wdt_status(void*) { return -1; }
inline int esp_task_wdt_reset() { return 0; }
