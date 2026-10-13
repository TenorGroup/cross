// X4 Pro builds only (TENOR_CABLE_FLASH); an empty unit elsewhere.
#if TENOR_CABLE_FLASH
#include "CableFlashLink.h"

#include <Arduino.h>
#include <HalPowerManager.h>
#include <HeapFloor.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>

#include "activities/ActivityManager.h"
#include "activities/settings/SdFirmwareUpdateActivity.h"
#include "CrossPointState.h"
#include "network/FirmwareBoardTag.h"

#ifndef SIMULATOR
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <esp_random.h>

#include "network/FirmwareFlasher.h"
#endif

namespace cableflash {
namespace {
unsigned long recoveryUntil = 0;

#ifndef SIMULATOR
const char* stateName(const esp_partition_t* part) {
  esp_ota_img_states_t state;
  if (!part || esp_ota_get_state_partition(part, &state) != ESP_OK) return "none";
  switch (state) {
    case ESP_OTA_IMG_VALID: return "valid";
    case ESP_OTA_IMG_PENDING_VERIFY: return "trial";
    case ESP_OTA_IMG_NEW: return "new";
    case ESP_OTA_IMG_UNDEFINED: return "undefined";
    default: return "invalid";
  }
}

// What the host needs before it sends anything: the board, the version, the trial state, the build
// that runs (ELF hash prefix) and both app slots as this device's partition table has them.
void hello() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  const esp_partition_t* boot = esp_ota_get_boot_partition();
  const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
  char elf[17] = "-";
  esp_app_desc_t desc{};
  if (running && esp_ota_get_partition_description(running, &desc) == ESP_OK) {
    for (int i = 0; i < 8; ++i) snprintf(elf + i * 2, 3, "%02x", desc.app_elf_sha256[i]);
  }
  char line[224];
  snprintf(line, sizeof(line),
           "TCF1 HELLO board=%.*s chip=%u version=%s cable=1 state=%s elf=%s running=%s@0x%lx+0x%lx boot=%s "
           "next=%s@0x%lx+0x%lx frame=%u",
           static_cast<int>(board_tag::boardNameLen()), board_tag::boardName(),
           static_cast<unsigned>(firmware_flash::runningPartitionChipId()), CROSSPOINT_VERSION, stateName(running), elf,
           running ? running->label : "-", running ? static_cast<unsigned long>(running->address) : 0UL,
           running ? static_cast<unsigned long>(running->size) : 0UL, boot ? boot->label : "-",
           next ? next->label : "-", next ? static_cast<unsigned long>(next->address) : 0UL,
           next ? static_cast<unsigned long>(next->size) : 0UL, static_cast<unsigned>(kPayload));
  say(line);
}

struct CablePort {
  size_t read(uint8_t* buf, const size_t max) {
    const int waiting = logSerial.available();
    if (waiting <= 0) {
      delay(1);
      return 0;
    }
    return logSerial.readBytes(buf, std::min(max, static_cast<size_t>(waiting)));
  }
  void reply(const char* line) { say(line); }
};

// The OTA slot that is not running, as the partition table on this device sizes it.
struct OtaSlot {
  const esp_partition_t* part = esp_ota_get_next_update_partition(nullptr);
  esp_ota_handle_t handle = 0;
  size_t size() const { return part ? part->size : 0; }
  bool begin(size_t) { return part && esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &handle) == ESP_OK; }
  bool write(const uint8_t* data, const size_t n) { return esp_ota_write(handle, data, n) == ESP_OK; }
  bool end() { return esp_ota_end(handle) == ESP_OK; }
  bool boot() { return esp_ota_set_boot_partition(part) == ESP_OK; }
  void abort() { esp_ota_abort(handle); }
};

struct MillisClock {
  uint32_t now() { return millis(); }
};
#endif
}  // namespace

void grantRecoveryConsent() { recoveryUntil = millis() + kConsentMs; }

void say(const char* line) {
#if LOG_SERIAL_HAS_TX_TIMEOUT
  sendLine(logSerial, line, 1);  // back to main.cpp's load-bearing 1 ms
#else
  logSerial.printf("%s\n", line);
#endif
}

void onLine(const char* line, GfxRenderer& renderer, MappedInputManager& input) {
  const char* rest = line + 5;  // after "TCF1 "
  if (std::strncmp(rest, "HELLO", 5) == 0 || std::strncmp(rest, "STATUS", 6) == 0) {
#ifndef SIMULATOR
    hello();
#endif
    return;
  }
  if (std::strncmp(rest, "BEGIN ", 6) != 0) return say("TCF1 FAIL command");
  Begin begin;
  if (!parseBegin(rest + 6, begin)) return say("TCF1 FAIL begin");
  const char* current = activityManager.currentName();
  const auto* cableActivity = SdFirmwareUpdateActivity::activeUpdate();
  const bool currentIsCable = cableActivity && cableActivity->isCable();
  const bool currentFailed = cableActivity && cableActivity->cableFailed();
  std::string returnBook = currentFailed ? cableActivity->cableBookToReturn() : std::string();
  const bool readerOpen = activityManager.isReaderActivity();
  if (readerOpen) returnBook = APP_STATE.openEpubPath;
  const bool updateOpen = cableActivity || (current && std::strcmp(current, "SdFirmwareUpdate") == 0);
  const bool confirmationOpen = current && std::strcmp(current, "Confirmation") == 0;
  const auto decision = decidePolicy(
      PolicyInput{false, readerOpen, confirmationOpen, updateOpen, currentIsCable, currentFailed, returnBook.c_str()});
  if (decision.action == PolicyAction::Reject) return say("TCF1 FAIL busy");
#ifndef SIMULATOR
  // An image on trial is accepted or rolled back first; the slot behind it is the way back.
  esp_ota_img_states_t state;
  if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK &&
      state == ESP_OTA_IMG_PENDING_VERIFY)
    return say("TCF1 FAIL trial");
#endif
  const bool consented = recoveryUntil != 0 && static_cast<long>(recoveryUntil - millis()) > 0;
  recoveryUntil = 0;  // one session per hold of the keys
  auto screen = makeUniqueNoThrow<SdFirmwareUpdateActivity>(renderer, input, begin, consented, std::move(returnBook));
  if (!screen) return say("TCF1 FAIL heap");
  LOG_INF("CABLE", "begin size=%lu version=%s consented=%d", static_cast<unsigned long>(begin.size), begin.version,
          consented);
  if (!consented) {
    char wait[24];
    snprintf(wait, sizeof(wait), "TCF1 WAIT %u", static_cast<unsigned>(kConsentMs / 1000));
    say(wait);
  }
  // A book is closed the usual way (its progress saved) before the slot is written.
  if (decision.action == PolicyAction::Replace)
    activityManager.replaceActivity(std::move(screen));
  else
    activityManager.pushActivity(std::move(screen));
}

#ifndef SIMULATOR
Result run(const Begin& begin, void (*progress)(size_t, size_t, void*), void* ctx) {
  HalPowerManager::Lock fullSpeed;
  const auto heapBefore = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  const auto largestBefore = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
  auto frame = heapfloor::array<uint8_t[]>(kFrame, "cable");
  if (!frame) {
    say("TCF1 FAIL heap");
    return Result::FlashFailed;
  }
  CablePort port;
  OtaSlot slot;
  MillisClock clock;
  const uint32_t session = esp_random();
  const unsigned long started = millis();
  LOG_INF("CABLE", "slot=%s@0x%lx+0x%lx size=%lu heap=%u largest=%u", slot.part ? slot.part->label : "-",
          slot.part ? static_cast<unsigned long>(slot.part->address) : 0UL, static_cast<unsigned long>(slot.size()),
          static_cast<unsigned long>(begin.size), static_cast<unsigned>(heapBefore), static_cast<unsigned>(largestBefore));
  // Every answer goes out whole through say() (sendLine); the log keeps the usual 1 ms.
  const Result result = receive(begin, session, firmware_flash::runningPartitionChipId(), port, slot, clock,
                                frame.get(), [&](size_t written, size_t total) { progress(written, total, ctx); });
  LOG_INF("CABLE", "end result=%s ms=%lu heap=%u min=%u", reason(result), millis() - started,
          static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
          static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)));
  return result;
}
#endif

}  // namespace cableflash

#endif  // TENOR_CABLE_FLASH
