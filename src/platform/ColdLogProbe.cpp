#include "ColdLogProbe.h"

#if defined(TENOR_PRESS_PROBE) && FREEINK_DEVICE_X3 && !defined(SIMULATOR)

#include <Arduino.h>
#include <Logging.h>
#include <esp_system.h>

#include <cstring>

namespace restartlog {
namespace {

constexpr uint32_t MAGIC = 0x434C5231;  // CLR1
// Keep the X3 probe inside its 8 KB RTC window.
constexpr size_t CAPACITY = 8;

struct Record {
  uint32_t sequence;
  uint32_t millisAt;
  uint32_t boot;
  uint8_t resetReason;
  uint8_t event;
  uint16_t reserved;
  uint32_t detail;
  uint32_t aux;
};

struct Ring {
  uint32_t magic;
  uint32_t next;
  uint32_t count;
  uint32_t boots;
  Record records[CAPACITY];
};

static_assert(sizeof(Ring) == 208);
RTC_NOINIT_ATTR Ring ring;

bool valid() {
  return ring.magic == MAGIC && ring.next < CAPACITY && ring.count <= CAPACITY;
}

void reset() {
  std::memset(&ring, 0, sizeof(ring));
  ring.magic = MAGIC;
}

void ensure() {
  if (!valid()) reset();
}

const char* name(const Event event) {
  switch (event) {
    case Event::Boot: return "boot";
    case Event::SilentRestart: return "silent-restart";
    case Event::StorageRestart: return "storage-restart";
    case Event::DeepSleepBegin: return "sleep-begin";
    case Event::DeepSleepReady: return "sleep-ready";
    case Event::FirmwareRestart: return "firmware-restart";
    case Event::OtaRestart: return "ota-restart";
    case Event::RecoveryRestart: return "recovery-restart";
    case Event::FramebufferRestart: return "framebuffer-restart";
    case Event::BleHeapRestart: return "ble-heap-restart";
    case Event::SleepEnter: return "sleep-enter";
    case Event::PayloadProbe: return "payload-probe";
    case Event::RestartNow: return "restart-now";
    case Event::PullStep: return "pull-step";
    case Event::StateRead: return "state-read";
    case Event::StateWrite: return "state-write";
    case Event::PullNetwork: return "pull-network";
    case Event::PullHeapRestart: return "pull-heap-restart";
  }
  return "unknown";
}

void append(const Event event, const uint32_t detail, const uint32_t aux) {
  ensure();
  Record& record = ring.records[ring.next];
  record.sequence = ring.count == 0 ? 1 : ring.records[(ring.next + CAPACITY - 1) % CAPACITY].sequence + 1;
  record.millisAt = millis();
  record.resetReason = static_cast<uint32_t>(esp_reset_reason());
  record.event = static_cast<uint8_t>(event);
  record.boot = ring.boots;
  record.detail = detail;
  record.aux = aux;
  ring.next = (ring.next + 1) % CAPACITY;
  if (ring.count < CAPACITY) ++ring.count;
}

}  // namespace

void boot(const bool silent, const uint32_t target, const uint32_t payload) {
  ensure();
  ++ring.boots;
  append(Event::Boot, target | (silent ? 0x80000000U : 0), payload);
}

void mark(const Event event, const uint32_t detail, const uint32_t aux) {
  append(event, detail, aux);
}

void dump() {
  ensure();
  logSerial.printf("COLDLOG_START:count=%lu,next=%lu,boots=%lu\n", static_cast<unsigned long>(ring.count),
                   static_cast<unsigned long>(ring.next), static_cast<unsigned long>(ring.boots));
  const size_t first = ring.count == CAPACITY ? ring.next : 0;
  for (size_t i = 0; i < ring.count; ++i) {
    const Record& record = ring.records[(first + i) % CAPACITY];
    logSerial.printf("COLDLOG:%lu,%lu,%lu,%s,%lu,%lu,%lu\n", static_cast<unsigned long>(record.sequence),
                     static_cast<unsigned long>(record.millisAt), static_cast<unsigned long>(record.resetReason),
                     name(static_cast<Event>(record.event)), static_cast<unsigned long>(record.detail),
                     static_cast<unsigned long>(record.aux), static_cast<unsigned long>(record.boot));
  }
  logSerial.printf("COLDLOG_END\n");
}

}  // namespace restartlog

#endif
