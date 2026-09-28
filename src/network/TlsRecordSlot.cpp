#include "TlsRecordSlot.h"

#if defined(FREEINK_NET_WOLFSSL) && !defined(SIMULATOR)
#include <Logging.h>
#include <wolfssl/ssl.h>
#include <wolfssl/wolfcrypt/memory.h>

#include <cstdlib>
#include <cstring>

namespace tls_slot {
namespace {

Slot slot(malloc, free);
wolfSSL_Malloc_cb previousMalloc = nullptr;
wolfSSL_Free_cb previousFree = nullptr;
wolfSSL_Realloc_cb previousRealloc = nullptr;

void* slotMalloc(const size_t size) {
  if (void* p = slot.take(size)) return p;
  return malloc(size);
}

void slotFree(void* p) {
  if (!slot.give(p)) free(p);
}

void* slotRealloc(void* p, const size_t size) {
  if (!slot.owns(p)) return realloc(p, size);
  if (size <= slot.blockBytes()) return p;
  void* grown = malloc(size);
  if (grown) {
    std::memcpy(grown, p, slot.blockBytes());
    slot.give(p);
  }
  return grown;
}

}  // namespace

Scope::Scope() {
  wolfSSL_GetAllocators(&previousMalloc, &previousFree, &previousRealloc);
  wolfSSL_SetAllocators(slotMalloc, slotFree, slotRealloc);
}

Scope::~Scope() {
  wolfSSL_SetAllocators(previousMalloc, previousFree, previousRealloc);
  LOG_PROBE("OTA", "TLS record slot served=%u fallbacks=%u", static_cast<unsigned>(slot.served()),
            static_cast<unsigned>(slot.fallbacks()));
  slot.end();
  slot = Slot(malloc, free);
}

}  // namespace tls_slot

#else

namespace tls_slot {
Scope::Scope() = default;
Scope::~Scope() = default;
}  // namespace tls_slot

#endif
