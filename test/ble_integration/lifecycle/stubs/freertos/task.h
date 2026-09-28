#pragma once
#include "FreeRTOS.h"
#include <stdexcept>
namespace scheduler {
inline void (*task)(void*) = nullptr;
inline void* parameter = nullptr;
inline bool createSucceeds = true;
inline unsigned creates = 0;
inline unsigned deletes = 0;
inline void run() {
  auto fn = task;
  auto arg = parameter;
  task = nullptr;
  parameter = nullptr;
  if (!fn) throw std::runtime_error("No worker queued");
  fn(arg);
}
}
inline BaseType_t xTaskCreate(void (*fn)(void*), const char*, uint32_t, void* arg, int, TaskHandle_t*) {
  ++scheduler::creates;
  if (!scheduler::createSucceeds) return pdFAIL;
  if (scheduler::task) throw std::runtime_error("Multiple queued workers");
  scheduler::task = fn;
  scheduler::parameter = arg;
  return pdPASS;
}
inline void vTaskDelete(TaskHandle_t) { ++scheduler::deletes; }
void delay(unsigned long amount);
#define pdMS_TO_TICKS(ms) (ms)
inline void vTaskDelay(uint32_t ticks) { delay(ticks); }
