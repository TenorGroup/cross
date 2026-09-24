#pragma once
using SemaphoreHandle_t = void*;
using TaskHandle_t = void*;
inline TaskHandle_t xTaskGetCurrentTaskHandle() { static int task; return &task; }
constexpr int portMAX_DELAY = 1;
inline SemaphoreHandle_t xSemaphoreCreateMutex() { static int mutex; return &mutex; }
inline void xSemaphoreTake(SemaphoreHandle_t, int) {}
inline void xSemaphoreGive(SemaphoreHandle_t) {}
