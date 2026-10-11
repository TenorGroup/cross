#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <utility>
#include <vector>
#define LOG_ERR(...) ((void)0)
unsigned long millis() { return 0; }
struct RenderLock {};
struct Box { int x0, y0, x1, y1; };
constexpr struct { Box box; } PLACES[6]{};
constexpr struct { int dx, dy; } LIFTS[6]{};
constexpr int LIFT_PAD = 4;
namespace logic { constexpr size_t FRAME_BYTES = 52272; }
namespace art { constexpr uint8_t DESK[] = {1}; }
static int decodes = 0, allocations = 0, judged = 0;
static bool psram = true, floorAllowed = true, scratch = true, valid = true;
struct HalMemory {
  using PsramBuffer = std::unique_ptr<uint8_t[]>;
  static PsramBuffer allocatePsram(size_t size) {
    ++allocations;
    return psram ? PsramBuffer(new uint8_t[size]) : nullptr;
  }
};
namespace heapfloor {
[[maybe_unused]] bool judge(size_t, const void*, const char*) { ++judged; return floorAllowed; }
template <typename T> std::unique_ptr<T> array(size_t size, const char*) {
  return scratch ? std::unique_ptr<T>(new uint8_t[size]) : nullptr;
}
}
bool decodeX3BrandPlane(const uint8_t*, size_t, uint8_t* output, size_t size) {
  ++decodes;
  for (size_t i = 0; i < size; ++i) output[i] = static_cast<uint8_t>(i * 71);
  return valid;
}
struct Renderer {
  std::vector<uint8_t> frame;
  void clearScreen() { frame.clear(); }
};
void liftArt(Renderer& renderer, const uint8_t* plane, Box, int, int) {
  renderer.frame.insert(renderer.frame.end(), plane, plane + logic::FRAME_BYTES);
}
struct Desk {
  Renderer renderer;
  HalMemory::PsramBuffer deskPlane;
  static constexpr int COUNT = 6;
  void render(RenderLock&&);
};
#include "background.inc"
int main() {
  int failed = 0;
  Desk warm;
  warm.render({});
  const auto reference = warm.renderer.frame;
  warm.render({});
  warm.render({});
  if (reference.empty() || warm.renderer.frame != reference || decodes != 1 || allocations != 1 || judged != 1) {
    ++failed;
    std::printf("FAIL warm decodes=%d alloc=%d judge=%d\n", decodes, allocations, judged);
  }
  for (int denied = 0; denied < 2; ++denied) {
    psram = denied != 0;
    floorAllowed = denied == 0;
    Desk cold;
    const int before = decodes;
    cold.render({});
    cold.render({});
    if (decodes != before + 2 || cold.renderer.frame != reference) ++failed;
  }
  psram = floorAllowed = true;
  valid = false;
  Desk damaged;
  damaged.render({});
  valid = true;
  damaged.render({});
  if (damaged.renderer.frame != reference) ++failed;
  psram = scratch = false;
  Desk empty;
  empty.render({});
  if (!empty.renderer.frame.empty()) ++failed;
  std::printf("desk repeated paint, PSRAM/floor refusal, decode retry, OOM: %d failures\n", failed);
  return failed ? 1 : 0;
}
