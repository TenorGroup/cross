#pragma once
typedef void* SemaphoreHandle_t;
inline SemaphoreHandle_t xSemaphoreCreateMutex() { return nullptr; }
inline int xSemaphoreTake(SemaphoreHandle_t, unsigned) { return 1; }
inline int xSemaphoreGive(SemaphoreHandle_t) { return 1; }
#ifndef portMAX_DELAY
#define portMAX_DELAY 0xffffffffUL
#endif
