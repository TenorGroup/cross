#include "FileTransferBackLatch.h"

#include <Arduino.h>
#include <BoardConfig.h>
#include <HalGPIO.h>

namespace {
#if !defined(SIMULATOR) && !CROSSPOINT_EMULATED
constexpr uint32_t POLL_MS = 5;
// The measured concurrent-ADC zero transients last 5-8 ms. Require 15 ms of
// stable state and discard an invalid conversion pair before arming an edge.
constexpr uint32_t STABLE_MS = 15;
// X3 diagnostic sampler used 872 bytes of its stack. Keep measurable headroom.
constexpr uint32_t STACK_BYTES = 2048;
#endif
}  // namespace

bool FileTransferBackLatch::start(HalGPIO& gpio, const uint8_t physicalBack) {
  stop();
  back = physicalBack;
#if defined(SIMULATOR) || CROSSPOINT_EMULATED
  (void)gpio;
  return true;
#else
  if (BoardConfig::ACTIVE.inputStyle != BoardConfig::InputStyle::XteinkAdcLadder || physicalBack > HalGPIO::BTN_DOWN)
    return true;
  input = &gpio;
  if (++generation == 0) ++generation;
  pending.store(0, std::memory_order_relaxed);
  stackFree.store(0, std::memory_order_relaxed);
  stopping.store(false, std::memory_order_relaxed);
  finished.store(false, std::memory_order_relaxed);
  if (xTaskCreate(poll, "transfer_back", STACK_BYTES, this, 1, &task) != pdPASS) {
    task = nullptr;
    input = nullptr;
    finished.store(true, std::memory_order_relaxed);
    return false;
  }
  return true;
#endif
}

void FileTransferBackLatch::stop() {
  if (!task) return;
  stopping.store(true, std::memory_order_release);
  // The worker acknowledges after its final access to this object. Wait before
  // activity destruction, Wi-Fi teardown or restart can invalidate its context.
  while (!finished.load(std::memory_order_acquire)) vTaskDelay(1);
  task = nullptr;
  input = nullptr;
  pending.store(0, std::memory_order_relaxed);
  if (++generation == 0) ++generation;
}

bool FileTransferBackLatch::consume() {
  return task && pending.exchange(0, std::memory_order_acquire) == generation;
}

void FileTransferBackLatch::poll(void* context) {
#if defined(SIMULATOR) || CROSSPOINT_EMULATED
  (void)context;
#else
  auto* const self = static_cast<FileTransferBackLatch*>(context);
  const uint32_t session = self->generation;
  const uint8_t physicalBack = self->back;
  HalGPIO* const gpio = self->input;
  bool armed = false;
  bool pressed = false;
  bool fired = false;
  bool candidateKnown = false;
  bool candidateDown = false;
  uint32_t candidateSince = 0;
  while (!self->stopping.load(std::memory_order_acquire)) {
    InputManager::ButtonAdcSample first{}, second{};
    gpio->sampleButtonAdc(first, second);
    const uint32_t now = millis();
    // Concurrent conversions were observed briefly returning zero for both
    // ladders. Treat that pair as invalid, including when Back is remapped to
    // physical key 3. Normal main input still handles real two-key chords.
    const bool valid = first.raw >= 0 && second.raw >= 0 && !(first.raw == 0 && second.raw == 0);
    if (!valid) {
      candidateKnown = false;
    } else {
      // Keys 0-3 classify on the first ladder, the side keys 4 and 5 on the second.
      const bool down = first.button == physicalBack || second.button == physicalBack;
      if (!candidateKnown || down != candidateDown) {
        candidateKnown = true;
        candidateDown = down;
        candidateSince = now;
      } else if (static_cast<uint32_t>(now - candidateSince) >= STABLE_MS) {
        if (!armed) {
          // Ignore a key held through entry until it has been released stably.
          if (!down) armed = true;
        } else if (down) {
          pressed = true;
        } else if (pressed && !fired) {
          fired = true;
          self->pending.store(session, std::memory_order_release);
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(POLL_MS) ? pdMS_TO_TICKS(POLL_MS) : 1);
  }
  self->stackFree.store(uxTaskGetStackHighWaterMark(nullptr), std::memory_order_relaxed);
  self->finished.store(true, std::memory_order_release);
  // No context access after the acknowledgement; stop() may now destroy it.
  vTaskDelete(nullptr);
#endif
}
