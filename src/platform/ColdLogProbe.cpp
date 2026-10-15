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
constexpr uint32_t TRACE_MAGIC = 0x43545231;  // CTR1
// Home failures finish after the seven radio/hash/join records. Keeping eight
// records preserves that complete failure trace while adding only 224 bytes
// to the measurement build's ordinary RAM.
constexpr size_t TRACE_CAPACITY = 8;

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

struct TraceRecord {
  uint32_t sequence;
  uint32_t millisAt;
  uint32_t boot;
  uint8_t stage;
  uint8_t reserved[3];
  int32_t code;
  uint32_t freeBytes;
  uint32_t largestBlock;
};

struct TraceRing {
  uint32_t magic;
  uint32_t next;
  uint32_t count;
  TraceRecord records[TRACE_CAPACITY];
};

static_assert(sizeof(Ring) == 208);
RTC_NOINIT_ATTR Ring ring;
// The detailed trace is measurement-only and is dumped before a pull restart.
// Keep it in ordinary RAM so the X3 RTC retention budget remains unchanged.
TraceRing traceRing;

bool valid() {
  return ring.magic == MAGIC && ring.next < CAPACITY && ring.count <= CAPACITY;
}

void reset() {
  std::memset(&ring, 0, sizeof(ring));
  ring.magic = MAGIC;
}

bool traceValid() {
  return traceRing.magic == TRACE_MAGIC && traceRing.next < TRACE_CAPACITY && traceRing.count <= TRACE_CAPACITY;
}

void resetTrace() {
  std::memset(&traceRing, 0, sizeof(traceRing));
  traceRing.magic = TRACE_MAGIC;
}

void ensureTrace() {
  if (!traceValid()) resetTrace();
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

const char* traceName(const TraceStage stage) {
  switch (stage) {
    case TraceStage::Hash: return "hash";
    case TraceStage::JoinStart: return "join-start";
    case TraceStage::WifiHint: return "wifi-hint";
    case TraceStage::WifiScan: return "wifi-scan";
    case TraceStage::WifiConnected: return "wifi-connected";
    case TraceStage::HttpHeap: return "http-heap";
    case TraceStage::HttpUrl: return "http-url";
    case TraceStage::HttpNtp: return "http-ntp";
    case TraceStage::HttpBegin: return "http-begin";
    case TraceStage::HttpTransport: return "http-transport";
    case TraceStage::HttpReply: return "http-reply";
    case TraceStage::HttpBody: return "http-body";
    case TraceStage::HttpParse: return "http-parse";
    case TraceStage::PullMap: return "pull-map";
    case TraceStage::PullSave: return "pull-save";
    case TraceStage::RadioBefore: return "radio-before";
    case TraceStage::RadioAfter: return "radio-after";
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

void traceReset() { resetTrace(); }

void trace(const TraceStage stage, const int32_t code, const uint32_t freeBytes, const uint32_t largestBlock) {
  ensure();
  ensureTrace();
  TraceRecord& record = traceRing.records[traceRing.next];
  record.sequence = traceRing.count == 0 ? 1 : traceRing.records[(traceRing.next + TRACE_CAPACITY - 1) % TRACE_CAPACITY].sequence + 1;
  record.millisAt = millis();
  record.boot = ring.boots;
  record.stage = static_cast<uint8_t>(stage);
  record.code = code;
  record.freeBytes = freeBytes;
  record.largestBlock = largestBlock;
  traceRing.next = (traceRing.next + 1) % TRACE_CAPACITY;
  if (traceRing.count < TRACE_CAPACITY) ++traceRing.count;
}

void dump() {
  ensure();
  ensureTrace();
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
  const size_t traceFirst = traceRing.count == TRACE_CAPACITY ? traceRing.next : 0;
  for (size_t i = 0; i < traceRing.count; ++i) {
    const TraceRecord& record = traceRing.records[(traceFirst + i) % TRACE_CAPACITY];
    logSerial.printf("COLDLOG_STAGE:%lu,%lu,%lu,%s,%ld,%lu,%lu\n", static_cast<unsigned long>(record.sequence),
                     static_cast<unsigned long>(record.millisAt), static_cast<unsigned long>(record.boot),
                     traceName(static_cast<TraceStage>(record.stage)), static_cast<long>(record.code),
                     static_cast<unsigned long>(record.freeBytes), static_cast<unsigned long>(record.largestBlock));
  }
  logSerial.printf("COLDLOG_END\n");
}

}  // namespace restartlog

#endif
