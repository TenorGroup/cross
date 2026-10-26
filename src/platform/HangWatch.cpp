#include "HangWatch.h"

#if defined(TENOR_PRESS_PROBE) && defined(FREEINK_DEVICE_X3) && FREEINK_DEVICE_X3 && !defined(SIMULATOR) && !defined(TENOR_NO_HANGWATCH)

#include <Arduino.h>
#include <Logging.h>
#include <esp_attr.h>
#include <esp_memory_utils.h>
#include <esp_private/freertos_debug.h>
#include <esp_rom_sys.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <riscv/rvruntime-frames.h>

#include <atomic>
#include <cstdio>

#include "ColdLogProbe.h"

namespace hangwatch {
namespace {

constexpr uint32_t MAGIC = 0x48414E47;  // HANG
constexpr size_t FRAMES = 5;  // the X3 probe's RTC memory has no room for more (12 overflowed it by 40 B)
constexpr const char* NAMES[2] = {"loop", "render"};

struct TaskRecord {
  uint32_t state;  // eTaskState: 1 ready, 2 blocked, 3 suspended
  uint32_t pc;     // where the task was switched out (mepc of its saved frame)
  uint32_t ra;
  uint32_t frames[FRAMES];  // code addresses found on its stack above that frame, innermost first
};

struct Record {
  uint32_t magic;
  uint32_t boot;  // restartlog's boot number when it tripped (COLDLOG_START boots= is one more after the restart)
  uint32_t atMs;
  uint32_t stuck;
  uint32_t loopAgeMs;
  uint32_t renderAgeMs;
  TaskRecord task[2];
};

RTC_NOINIT_ATTR Record record;

std::atomic<uint32_t> loopSeen{0};
std::atomic<uint32_t> renderSince{0};
TaskHandle_t watched[2] = {nullptr, nullptr};
StaticTask_t watchTcb;
StackType_t watchStack[2048];

void capture(TaskHandle_t task, TaskRecord& out) {
  out = TaskRecord{};
  if (!task) return;
  out.state = static_cast<uint32_t>(eTaskGetState(task));
  TaskSnapshot_t snap{};
  if (vTaskGetSnapshot(task, &snap) != pdTRUE || !snap.pxTopOfStack || snap.pxTopOfStack >= snap.pxEndOfStack) return;
  // A task that is not running has its registers in an RvExcFrame at the top of its stack.
  const auto* frame = reinterpret_cast<const RvExcFrame*>(snap.pxTopOfStack);
  out.pc = static_cast<uint32_t>(frame->mepc);
  out.ra = static_cast<uint32_t>(frame->ra);
  const auto* word = reinterpret_cast<const uint32_t*>(reinterpret_cast<const uint8_t*>(snap.pxTopOfStack) + RV_STK_FRMSZ);
  const auto* end = reinterpret_cast<const uint32_t*>(snap.pxEndOfStack);
  // ponytail: a stack scan, not an unwind (no frame pointers on the C3); stale words can show up. The core dump has
  // the exact chains.
  for (size_t n = 0; word < end && n < FRAMES; ++word) {
    if ((*word & 1u) == 0 && esp_ptr_executable(reinterpret_cast<const void*>(*word))) out.frames[n++] = *word;
  }
}

void print(const char* prefix) {
  esp_rom_printf("%s stuck=%u boot=%u at=%u loop_age=%u render_age=%u\n", prefix, unsigned(record.stuck),
                 unsigned(record.boot), unsigned(record.atMs), unsigned(record.loopAgeMs), unsigned(record.renderAgeMs));
  for (int i = 0; i < 2; ++i) {
    const TaskRecord& t = record.task[i];
    esp_rom_printf("%s_TASK %s state=%u pc=0x%08x ra=0x%08x bt=", prefix, NAMES[i], unsigned(t.state), unsigned(t.pc),
                   unsigned(t.ra));
    for (size_t f = 0; f < FRAMES && t.frames[f]; ++f) esp_rom_printf(" 0x%08x", unsigned(t.frames[f]));
    esp_rom_printf("\n");
  }
}

// Cable only (no room in RTC): every task, so a task of higher priority that spins (both watched tasks then show
// as ready, never running) is named by its PC.
void printAllTasks() {
  TaskSnapshot_t snaps[24];
  UBaseType_t tcbSize = 0;
  const UBaseType_t n = uxTaskGetSnapshotAll(snaps, 24, &tcbSize);
  for (UBaseType_t i = 0; i < n; ++i) {
    const auto task = static_cast<TaskHandle_t>(snaps[i].pxTCB);
    const auto* frame = reinterpret_cast<const RvExcFrame*>(snaps[i].pxTopOfStack);
    esp_rom_printf("HANGWATCH_ALL %s prio=%u state=%u pc=0x%08x ra=0x%08x\n", pcTaskGetName(task),
                   unsigned(uxTaskPriorityGet(task)), unsigned(eTaskGetState(task)),
                   task == xTaskGetCurrentTaskHandle() ? 0u : unsigned(frame->mepc),
                   task == xTaskGetCurrentTaskHandle() ? 0u : unsigned(frame->ra));
  }
}

void run(void*) {
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    const uint32_t now = millis();
    const uint32_t seen = loopSeen.load(std::memory_order_relaxed);
    const uint32_t since = renderSince.load(std::memory_order_relaxed);
    const uint8_t s = stuck(now, seen, since);
    if (!s) continue;
    // No task switch from here to the abort: every saved frame read below stays put.
    vTaskSuspendAll();
    record.magic = 0;
    record.boot = restartlog::bootNumber();
    record.atMs = now;
    record.stuck = s;
    record.loopAgeMs = now - seen;
    record.renderAgeMs = since ? now - since : 0;
    capture(watched[0], record.task[0]);
    capture(watched[1], record.task[1]);
    record.magic = MAGIC;
    print("HANGWATCH");
    printAllTasks();
    char why[96];
    snprintf(why, sizeof(why), "HANGWATCH stuck=%u loop pc=0x%08lx render pc=0x%08lx", s,
             static_cast<unsigned long>(record.task[0].pc), static_cast<unsigned long>(record.task[1].pc));
    esp_system_abort(why);
  }
}

}  // namespace

void loopAlive() {
  const TaskHandle_t self = xTaskGetCurrentTaskHandle();
  if (!watched[0]) {
    watched[0] = self;
    loopSeen.store(millis(), std::memory_order_relaxed);
    xTaskCreateStatic(run, "hangwatch", sizeof(watchStack), nullptr, configMAX_PRIORITIES - 1, watchStack, &watchTcb);
    return;
  }
  if (self == watched[0]) loopSeen.store(millis(), std::memory_order_relaxed);
}

void renderBusy(const bool busy) {
  if (!watched[1]) watched[1] = xTaskGetCurrentTaskHandle();
  renderSince.store(busy ? (millis() | 1u) : 0, std::memory_order_relaxed);
}

void dump() {
  if (record.magic != MAGIC) {
    logSerial.printf("COLDLOG_HANG:none\n");
    return;
  }
  logSerial.printf("COLDLOG_HANG:stuck=%lu,boot=%lu,at=%lu,loop_age=%lu,render_age=%lu\n",
                   static_cast<unsigned long>(record.stuck), static_cast<unsigned long>(record.boot),
                   static_cast<unsigned long>(record.atMs), static_cast<unsigned long>(record.loopAgeMs),
                   static_cast<unsigned long>(record.renderAgeMs));
  for (int i = 0; i < 2; ++i) {
    const TaskRecord& t = record.task[i];
    logSerial.printf("COLDLOG_HANG_TASK:%s,state=%lu,pc=0x%08lx,ra=0x%08lx,bt=", NAMES[i],
                     static_cast<unsigned long>(t.state), static_cast<unsigned long>(t.pc),
                     static_cast<unsigned long>(t.ra));
    for (size_t f = 0; f < FRAMES && t.frames[f]; ++f) logSerial.printf(" 0x%08lx", static_cast<unsigned long>(t.frames[f]));
    logSerial.printf("\n");
  }
}

}  // namespace hangwatch

#endif
