#pragma once
using SemaphoreHandle_t = void*;
using TaskHandle_t = void*;
// Which task is running: a test switches it to call from the render task.
namespace faketask {
inline int loop, render;
inline TaskHandle_t current = &loop;
}
inline TaskHandle_t xTaskGetCurrentTaskHandle() { return faketask::current; }
constexpr int portMAX_DELAY = 1;
inline SemaphoreHandle_t xSemaphoreCreateMutex() { static int mutex; return &mutex; }
inline void xSemaphoreTake(SemaphoreHandle_t, int) {}
inline void xSemaphoreGive(SemaphoreHandle_t) {}
