#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <map>
#include <algorithm>
#include <wolfssl/wolfcrypt/memory.h>
#include <HeapFloor.h>

static std::map<void*, size_t> blocks;
static bool refuse = false;
static int judged = 0, failures = 0;
static size_t peak = 0;
static void check(bool ok, const char* text) {
  std::printf("%s %s\n", ok ? "PASS" : "FAIL", text);
  if (!ok) ++failures;
}
static void* trackedMalloc(size_t n) {
  void* p = std::malloc(n);
  if (p) blocks[p] = n;
  size_t live = 0;
  for (const auto& b : blocks) live += b.second;
  peak = std::max(peak, live);
  return p;
}
static void trackedFree(void* p) { blocks.erase(p); std::free(p); }
static void* trackedRealloc(void* p, size_t n) {
  void* q = std::realloc(p, n);
  if (q || !n) { blocks.erase(p); if (q) blocks[q] = n; }
  return q;
}
size_t heap_caps_get_allocated_size(void* p) { return blocks.at(p); }
void* heapfloor::take(size_t n, const char*) { ++judged; return refuse ? nullptr : trackedMalloc(n); }
static wolfSSL_Malloc_cb alloc = trackedMalloc;
static wolfSSL_Free_cb release = trackedFree;
static wolfSSL_Realloc_cb resize = trackedRealloc;
int wolfSSL_GetAllocators(wolfSSL_Malloc_cb* a, wolfSSL_Free_cb* f, wolfSSL_Realloc_cb* r) {
  *a = alloc; *f = release; *r = resize; return 0;
}
int wolfSSL_SetAllocators(wolfSSL_Malloc_cb a, wolfSSL_Free_cb f, wolfSSL_Realloc_cb r) {
  alloc = a; release = f; resize = r; return 0;
}
#define malloc trackedMalloc
#define free trackedFree
#define realloc trackedRealloc
#include "../../src/network/TlsRecordSlot.cpp"
#undef malloc
#undef free
#undef realloc

int main() {
  {
    tls_slot::Scope scope;
    void* p = alloc(16401);
    check(p && judged == 1, "record reservation uses the floor");
    void* second = alloc(16401);
    check(second && second != p, "busy slot falls back through the floor");
    release(second);
    void* oversized = alloc(tls_slot::BYTES + 512);
    check(oversized != nullptr, "oversized record falls back");
    release(oversized);
    if (p) std::memset(p, 42, 16401);
    refuse = true;
    check(resize(p, tls_slot::BYTES + 512) == nullptr, "record growth refusal returns null");
    check(scope.failed(), "refusal is reported to the updater");
    check(p && static_cast<unsigned char*>(p)[16400] == 42, "record growth refusal keeps the old bytes");
    check(alloc(4096) == nullptr && alloc(16401) == nullptr, "all large fallback refusals return null");
    refuse = false;
    release(p);
  }
  check(blocks.empty(), "scope releases its idle record");
  {
    tls_slot::Scope scope;
    void* p = alloc(6000);
    if (p) std::memset(p, 71, 6000);
    refuse = true;
    const int before = judged;
    check(resize(p, 20000) == nullptr && judged > before, "fallback growth uses the floor and refuses cleanly");
    check(p && blocks.count(p) && static_cast<unsigned char*>(p)[5999] == 71, "fallback refusal keeps the old block");
    refuse = false;
    void* grown = resize(p, 20000);
    check(grown && static_cast<unsigned char*>(grown)[5999] == 71, "successful growth preserves old bytes");
    release(grown ? grown : p);
    void* fromNull = resize(nullptr, 5000);
    check(fromNull != nullptr, "realloc null allocates through the floor");
    check(resize(fromNull, 0) == nullptr, "realloc zero releases the block");
  }
  check(blocks.empty(), "all fallback blocks are released");
  check(alloc == trackedMalloc && release == trackedFree && resize == trackedRealloc, "scope restores allocators");
  std::printf("TLS peak=%zu judged=%d failures=%d\n", peak, judged, failures);
  return failures != 0;
}
