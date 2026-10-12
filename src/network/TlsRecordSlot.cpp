#include "TlsRecordSlot.h"

#if defined(FREEINK_NET_WOLFSSL) && !defined(SIMULATOR)
#include <Logging.h>
#include <HeapFloor.h>
#include <esp_heap_caps.h>
#include <wolfssl/ssl.h>
#include <wolfssl/wolfcrypt/memory.h>

#include <cstdlib>
#include <cstring>

namespace tls_slot {
namespace {

void* floorMalloc(const size_t size) { return heapfloor::take(size, "tls-record-slot"); }

Slot slot(floorMalloc, free);
wolfSSL_Malloc_cb previousMalloc = nullptr;
wolfSSL_Free_cb previousFree = nullptr;
wolfSSL_Realloc_cb previousRealloc = nullptr;
bool allocationFailed = false;

void* slotMalloc(const size_t size) {
  if (void* p = slot.take(size)) return p;
  void* p = size >= heapfloor::kBigBytes ? heapfloor::take(size, "tls-fallback") : malloc(size);
  if (!p && size) allocationFailed = true;
  return p;
}

void slotFree(void* p) {
  if (!slot.give(p)) free(p);
}

void* slotRealloc(void* p, const size_t size) {
  if (!p) return slotMalloc(size);
  if (!size) {
    slotFree(p);
    return nullptr;
  }
  if (!slot.owns(p)) {
    if (size < heapfloor::kBigBytes) {
      void* resized = realloc(p, size);
      if (!resized) allocationFailed = true;
      return resized;
    }
    const size_t oldSize = heap_caps_get_allocated_size(p);
    void* grown = heapfloor::take(size, "tls-fallback-realloc");
    if (!grown) {
      allocationFailed = true;
      return nullptr;
    }
    std::memcpy(grown, p, oldSize < size ? oldSize : size);
    free(p);
    return grown;
  }
  if (size <= slot.blockBytes()) return p;
  void* grown = heapfloor::take(size, "tls-record-realloc");
  if (grown) {
    std::memcpy(grown, p, slot.blockBytes());
    slot.give(p);
  } else {
    allocationFailed = true;
  }
  return grown;
}

}  // namespace

Scope::Scope() {
  allocationFailed = false;
  wolfSSL_GetAllocators(&previousMalloc, &previousFree, &previousRealloc);
  wolfSSL_SetAllocators(slotMalloc, slotFree, slotRealloc);
}

bool Scope::failed() const { return allocationFailed; }

Scope::~Scope() {
  wolfSSL_SetAllocators(previousMalloc, previousFree, previousRealloc);
  LOG_PROBE("OTA", "TLS record slot served=%u fallbacks=%u", static_cast<unsigned>(slot.served()),
            static_cast<unsigned>(slot.fallbacks()));
  slot.end();
  slot = Slot(floorMalloc, free);
}

}  // namespace tls_slot

#else

namespace tls_slot {
Scope::Scope() = default;
Scope::~Scope() = default;
bool Scope::failed() const { return false; }
}  // namespace tls_slot

#endif
