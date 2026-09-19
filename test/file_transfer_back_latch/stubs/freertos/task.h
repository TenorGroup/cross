#pragma once
#include "FreeRTOS.h"
struct FakeTask;
using TaskHandle_t = FakeTask*;
int xTaskCreate(void (*fn)(void*), const char*, uint32_t, void*, unsigned, TaskHandle_t*);
void vTaskDelay(TickType_t);
void vTaskDelete(TaskHandle_t);
unsigned uxTaskGetStackHighWaterMark(TaskHandle_t);
