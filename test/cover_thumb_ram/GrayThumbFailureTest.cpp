#include <Arduino.h>
#include <GrayThumb.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <new>

EspClass ESP;
namespace {
int untilFail = -1;
struct Block { void* p; size_t n; } blocks[64]{};
size_t live = 0;
void* take(size_t n) {
  if (untilFail == 0) { untilFail = -1; return nullptr; }
  if (untilFail > 0) --untilFail;
  void* p = std::malloc(n ? n : 1);
  if (p) for (auto& b : blocks) if (!b.p) { b = {p, n}; live += n; return p; }
  assert(!p);
  return nullptr;
}
void drop(void* p) {
  if (!p) return;
  for (auto& b : blocks) if (b.p == p) { live -= b.n; b = {}; std::free(p); return; }
  assert(false);
}
bool feed(GrayThumb& t) {
  uint8_t row[450]{};
  for (int y = 0; y < 675; ++y) t.block(0, y, 450, 1, row, 450);
  return t.finish();
}
}
void* operator new(size_t n) { void* p = take(n); if (!p) throw std::bad_alloc(); return p; }
void* operator new[](size_t n) { return ::operator new(n); }
void* operator new(size_t n, const std::nothrow_t&) noexcept { return take(n); }
void* operator new[](size_t n, const std::nothrow_t&) noexcept { return take(n); }
void operator delete(void* p) noexcept { drop(p); }
void operator delete[](void* p) noexcept { drop(p); }
void operator delete(void* p, size_t) noexcept { drop(p); }
void operator delete[](void* p, size_t) noexcept { drop(p); }
int main() {
  for (int failure = 0; failure < 4; ++failure) {
    GrayThumb t(226);
    untilFail = failure;
    assert(!t.start(450, 675, 8));
    std::printf("allocation=%d retained=%zu\n", failure, live);
    std::fflush(stdout);
    assert(live == 0 && !t.ready());
    assert(t.start(450, 675, 8) && feed(t) && t.ready());
    assert(!t.start(0, 675, 8) && live == 0 && !t.ready());
  }
  for (int failure = 0; failure < 4; ++failure) {
    GrayThumb big(450), small(226);
    big.alsoFeed(&small);
    untilFail = 4 + failure;
    assert(big.start(450, 675, 8));
    assert(live == GrayThumb::bufferBytes(450, 450, 675, 8));
    assert(feed(big) && big.ready() && !small.ready());
  }
  assert(live == 0);
  std::puts("GREEN all failed starts release their buffers and keep the parent usable");
}
