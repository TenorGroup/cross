#if defined(TENOR_HEAP_BIG) && !defined(SIMULATOR)
#include "HeapBigProbe.h"

#include <Arduino.h>
#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstring>

// The libc allocators (newlib heap.c) all end in heap_caps_malloc_default, heap_caps_realloc_default or
// heap_caps_aligned_alloc_default; the radio's controller calls heap_caps_malloc and heap_caps_calloc.
// --wrap only sees calls between object files, so each entry point is wrapped on its own.
extern "C" {
void* __real_heap_caps_malloc(size_t size, uint32_t caps);
void* __real_heap_caps_calloc(size_t n, size_t size, uint32_t caps);
void* __real_heap_caps_realloc(void* ptr, size_t size, uint32_t caps);
void* __real_heap_caps_malloc_default(size_t size);
void* __real_heap_caps_realloc_default(void* ptr, size_t size);
void* __real_heap_caps_aligned_alloc(size_t alignment, size_t size, uint32_t caps);
void* __real_heap_caps_aligned_alloc_default(size_t alignment, size_t size);
void* __real_heap_caps_aligned_calloc(size_t alignment, size_t n, size_t size, uint32_t caps);
}

namespace {
constexpr uint32_t kDefaultCaps = MALLOC_CAP_DEFAULT | MALLOC_CAP_INTERNAL;  // what malloc() asks for
constexpr uint32_t kRing = 64;

struct Rec {
  uint32_t ms;
  uint32_t size;
  uint32_t caps;
  uint32_t ok;
  uint32_t freeAfter;
  uint32_t largestAfter;
  uint32_t strip;
  char task[12];
};

DRAM_ATTR Rec ring[kRing];
DRAM_ATTR volatile uint32_t head = 0;
DRAM_ATTR volatile uint32_t tail = 0;
DRAM_ATTR volatile uint32_t dropped = 0;
DRAM_ATTR volatile uint32_t radioTaskBytes = 0;
DRAM_ATTR TaskHandle_t volatile stripTask = nullptr;
DRAM_ATTR portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

// Runs inside the allocator, maybe from an ISR or with the cache off: IRAM, no locks of its own
// beyond a critical section, no allocation, no logging.
IRAM_ATTR void note(const size_t size, const uint32_t caps, const void* result) {
  const bool isr = xPortInIsrContext();
  // Before the scheduler runs there is no task to name (pcTaskGetName asserts).
  const bool early = xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED;
  const char* name = isr ? "isr" : early ? "boot" : pcTaskGetName(nullptr);
  const bool radioTask = !isr && !early && heapbig::radioTaskName(name);
  const TaskHandle_t holder = stripTask;
  const bool inStrip = !isr && !early && holder != nullptr && xTaskGetCurrentTaskHandle() != holder;
  const bool keep = heapbig::record(size, inStrip, result != nullptr);
  if (!radioTask && !keep) return;
  // The heap after a big allocation, read outside the critical section and never from an ISR.
  // Risk: the size getters are not IRAM code; an allocation made with the cache off would fault
  // here. Measurement builds only.
  uint32_t freeAfter = 0;
  uint32_t largestAfter = 0;
  if (keep && !isr) {
    freeAfter = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    largestAfter = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }
  portENTER_CRITICAL_SAFE(&mux);
  if (radioTask && result) radioTaskBytes = radioTaskBytes + size;
  if (keep) {
    if (head - tail >= kRing) {
      dropped = dropped + 1;
    } else {
      Rec& r = ring[head % kRing];
      r.ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);
      r.size = size;
      r.caps = caps;
      r.ok = result != nullptr;
      r.freeAfter = freeAfter;
      r.largestAfter = largestAfter;
      r.strip = inStrip ? 1 : 0;
      size_t i = 0;
      for (; name && name[i] && i < sizeof(r.task) - 1; i++) r.task[i] = name[i];
      r.task[i] = 0;
      head = head + 1;
    }
  }
  portEXIT_CRITICAL_SAFE(&mux);
}
}  // namespace

extern "C" {
IRAM_ATTR void* __wrap_heap_caps_malloc(size_t size, uint32_t caps) {
  void* p = __real_heap_caps_malloc(size, caps);
  note(size, caps, p);
  return p;
}
IRAM_ATTR void* __wrap_heap_caps_calloc(size_t n, size_t size, uint32_t caps) {
  void* p = __real_heap_caps_calloc(n, size, caps);
  note(n * size, caps, p);
  return p;
}
IRAM_ATTR void* __wrap_heap_caps_realloc(void* ptr, size_t size, uint32_t caps) {
  void* p = __real_heap_caps_realloc(ptr, size, caps);
  note(size, caps, p);
  return p;
}
IRAM_ATTR void* __wrap_heap_caps_malloc_default(size_t size) {
  void* p = __real_heap_caps_malloc_default(size);
  note(size, kDefaultCaps, p);
  return p;
}
IRAM_ATTR void* __wrap_heap_caps_realloc_default(void* ptr, size_t size) {
  void* p = __real_heap_caps_realloc_default(ptr, size);
  note(size, kDefaultCaps, p);
  return p;
}
IRAM_ATTR void* __wrap_heap_caps_aligned_alloc(size_t alignment, size_t size, uint32_t caps) {
  void* p = __real_heap_caps_aligned_alloc(alignment, size, caps);
  note(size, caps, p);
  return p;
}
IRAM_ATTR void* __wrap_heap_caps_aligned_alloc_default(size_t alignment, size_t size) {
  void* p = __real_heap_caps_aligned_alloc_default(alignment, size);
  note(size, kDefaultCaps, p);
  return p;
}
IRAM_ATTR void* __wrap_heap_caps_aligned_calloc(size_t alignment, size_t n, size_t size, uint32_t caps) {
  void* p = __real_heap_caps_aligned_calloc(alignment, n, size, caps);
  note(n * size, caps, p);
  return p;
}
}

void heapBigStripWindow(const bool held) { stripTask = held ? xTaskGetCurrentTaskHandle() : nullptr; }

void heapBigProbeTick(const bool radioRunning) {
  static uint32_t printedBytes = 0;
  while (tail != head) {
    Rec r;
    portENTER_CRITICAL_SAFE(&mux);
    r = ring[tail % kRing];
    tail = tail + 1;
    portEXIT_CRITICAL_SAFE(&mux);
    Serial.printf("[BIG] t=%lu size=%u caps=0x%x task=%s ok=%u free=%u largest=%u strip=%u\n",
                  static_cast<unsigned long>(r.ms), static_cast<unsigned>(r.size), static_cast<unsigned>(r.caps), r.task,
                  static_cast<unsigned>(r.ok), static_cast<unsigned>(r.freeAfter),
                  static_cast<unsigned>(r.largestAfter), static_cast<unsigned>(r.strip));
    // Inside the strip window the floor question is still open (founder); those lines say strip=1.
    if (!r.strip && r.task[0] != 'i' && heapfloor::crosses(r.freeAfter, r.largestAfter)) {
      // Same one-line shape as the paint marks, for the release gate.
      Serial.printf("[HEAP_FLOOR] cross where=ALLOC size=%u task=%s t=%lu free=%u largest=%u floor_free=%u floor_largest=%u\n",
                    static_cast<unsigned>(r.size), r.task, static_cast<unsigned long>(r.ms),
                    static_cast<unsigned>(r.freeAfter), static_cast<unsigned>(r.largestAfter),
                    static_cast<unsigned>(heapfloor::kFreeFloor), static_cast<unsigned>(heapfloor::kLargestFloorReported));
    }
  }
  const uint32_t bytes = radioTaskBytes;
  if (bytes != printedBytes || dropped) {
    printedBytes = bytes;
    Serial.printf("[BIG] radio_task_bytes=%u dropped=%u radio_up=%u\n", static_cast<unsigned>(bytes),
                  static_cast<unsigned>(dropped), radioRunning ? 1u : 0u);
  }
}
#endif
