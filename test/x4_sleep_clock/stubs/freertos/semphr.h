#pragma once
using SemaphoreHandle_t = void*;
constexpr int portMAX_DELAY = 1;
inline SemaphoreHandle_t xSemaphoreCreateMutex() { static int mutex; return &mutex; }
inline void xSemaphoreTake(SemaphoreHandle_t, int) {}
inline void xSemaphoreGive(SemaphoreHandle_t) {}
