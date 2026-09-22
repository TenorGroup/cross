#pragma once

using SemaphoreHandle_t = void*;
constexpr int portMAX_DELAY = 0;
inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutex() { return reinterpret_cast<void*>(1); }
inline void xSemaphoreTakeRecursive(SemaphoreHandle_t, int) {}
inline void xSemaphoreGiveRecursive(SemaphoreHandle_t) {}
