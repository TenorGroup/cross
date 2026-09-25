#pragma once

#include <atomic>
#include <cstdint>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

class HalGPIO;

// Capture a physical Back tap while a synchronous transfer blocks main input.
// Main remains the sole owner of normal input events and activity transitions.
class FileTransferBackLatch {
 public:
  FileTransferBackLatch() = default;
  ~FileTransferBackLatch() { stop(); }
  FileTransferBackLatch(const FileTransferBackLatch&) = delete;
  FileTransferBackLatch& operator=(const FileTransferBackLatch&) = delete;

  // Unsupported boards keep normal input. False means the sampler allocation
  // failed on a supported board. The mapping is fixed for this session.
  bool start(HalGPIO& gpio, uint8_t physicalBack);
  void stop();
  bool consume();
  // A tap waiting for consume(), left in place. Safe from the upload handler.
  bool latched() const { return task && pending.load(std::memory_order_acquire) == generation; }
  bool active() const { return task != nullptr; }
  uint32_t stackFreeBytes() const { return stackFree.load(std::memory_order_relaxed); }

 private:
  static void poll(void* context);
  TaskHandle_t task = nullptr;
  HalGPIO* input = nullptr;
  uint8_t back = 0;
  uint32_t generation = 0;
  std::atomic<uint32_t> pending{0};
  std::atomic<bool> stopping{false};
  std::atomic<bool> finished{true};
  std::atomic<uint32_t> stackFree{0};
};
