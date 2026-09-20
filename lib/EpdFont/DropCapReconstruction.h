#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>

// Bounded bitmap contour reconstruction. Retain source bytes until all passes finish.
namespace dropcap_reconstruction {
constexpr int maxWidth = 256;
constexpr int maxHeight = 256;
constexpr size_t maxScratchBytes = 4 * maxWidth + 16 * (maxWidth + 2) + 4;

struct Bitmap {
  const uint8_t* bytes;
  int width;
  int height;
  bool twoBit;
  int sample(int x, int y) const {
    if (x < 0 || y < 0 || x >= width || y >= height) return 0;
    const int i = y * width + x;
    return twoBit ? (bytes[i / 4] >> (6 - 2 * (i % 4))) & 3 : ((bytes[i / 8] >> (7 - i % 8)) & 1) * 3;
  }
};

inline unsigned integerRoot(unsigned n) {
  unsigned result = 0;
  unsigned bit = 1u << 30;
  while (bit > n) bit >>= 2;
  while (bit) {
    if (n >= result + bit) { n -= result + bit; result = (result >> 1) + bit; }
    else result >>= 1;
    bit >>= 2;
  }
  return result;
}

inline int distanceAt(const Bitmap& b, int x, int y) {
  unsigned best = 512u * 512u;
  // Only crossings within 2 source pixels can influence a non-saturated sample.
  for (int yy = y - 2; yy <= y + 1; ++yy) {
    for (int xx = x - 2; xx <= x + 1; ++xx) {
      const int a = b.sample(xx, yy);
      for (int direction = 0; direction < 2; ++direction) {
        const int v = b.sample(xx + (direction == 0), yy + (direction == 1));
        if ((a >= 2) == (v >= 2)) continue;
        const int t = (3 - 2 * a) * 128 / (v - a);
        const int dx = (xx - x) * 256 + (direction == 0 ? t : 0);
        const int dy = (yy - y) * 256 + (direction == 1 ? t : 0);
        const unsigned d = static_cast<unsigned>(dx * dx + dy * dy);
        best = std::min(best, d);
      }
    }
  }
  const unsigned floor = integerRoot(best);
  const int d = static_cast<int>(floor + (best - floor * floor > floor));
  return b.sample(x, y) >= 2 ? d : -d;
}

class RowSampler {
  Bitmap source_;
  int16_t* rows_;
  int cached_[2] = {-10000, -10000};
 public:
  RowSampler(Bitmap b, int16_t* rows) : source_(b), rows_(rows) {}
  int distance(int x, int y) {
    if (x < 0 || y < 0 || x >= source_.width || y >= source_.height) return -128;
    const int slot = y & 1;
    if (cached_[slot] != y) {
      for (int xx = 0; xx < source_.width; ++xx) rows_[slot * source_.width + xx] = distanceAt(source_, xx, y);
      cached_[slot] = y;
    }
    return rows_[slot * source_.width + x];
  }
  int ink(int x, int y, int w, int h, bool reconstructed) {
    const int fx = ((2 * x + 1) * source_.width * 128 / w) - 128;
    const int fy = ((2 * y + 1) * source_.height * 128 / h) - 128;
    const int sx = fx >= 0 ? fx / 256 : -1, wx = fx - sx * 256;
    const int sy = fy >= 0 ? fy / 256 : -1, wy = fy - sy * 256;
    if (!reconstructed) {
      return (source_.sample(sx, sy) * (256 - wx) * (256 - wy) + source_.sample(sx + 1, sy) * wx * (256 - wy) +
              source_.sample(sx, sy + 1) * (256 - wx) * wy + source_.sample(sx + 1, sy + 1) * wx * wy + 32768) / 65536;
    }
    const int sum = distance(sx, sy) * (256 - wx) * (256 - wy) + distance(sx + 1, sy) * wx * (256 - wy) +
                    distance(sx, sy + 1) * (256 - wx) * wy + distance(sx + 1, sy + 1) * wx * wy;
    // ink = clamp(round(1.5 + 2 * signed_distance), 0, 3).
    const int numerator = 512 * 65536 + 2 * sum;
    return numerator <= 0 ? 0 : std::min(3, numerator / (256 * 65536));
  }
};

// Streaming connected components. Storage is 4 * width + 1 uint16_t entries.
class Components {
  int width_;
  bool diagonal_;
  uint16_t* previous_;
  uint16_t* current_;
  uint16_t* parents_;
  int next_ = 1;
  int total_ = 0;
  uint16_t root(uint16_t id) {
    while (parents_[id] != id) { parents_[id] = parents_[parents_[id]]; id = parents_[id]; }
    return id;
  }
 public:
  Components(int width, bool diagonal, uint16_t* data) : width_(width), diagonal_(diagonal), previous_(data),
      current_(data + width), parents_(data + 2 * width) {
    std::fill(data, data + 4 * width + 1, 0);
  }
  void pixel(int x, bool filled) {
    if (!filled) { current_[x] = 0; return; }
    uint16_t chosen = 0;
    const auto join = [&](uint16_t id) {
      if (!id) return;
      id = root(id);
      if (!chosen) chosen = id;
      else if (chosen != id) { parents_[id] = chosen; --total_; }
    };
    if (x > 0) join(current_[x - 1]);
    if (diagonal_ && x > 0) join(previous_[x - 1]);
    join(previous_[x]);
    if (diagonal_ && x + 1 < width_) join(previous_[x + 1]);
    if (!chosen) { chosen = next_++; parents_[chosen] = chosen; ++total_; }
    current_[x] = chosen;
  }
  void endRow() {
    for (int x = 0; x < width_; ++x) if (current_[x]) current_[x] = root(current_[x]);
    std::fill(parents_, parents_ + 2 * width_ + 1, 0);
    next_ = 1;
    for (int x = 0; x < width_; ++x) {
      const int old = current_[x];
      if (old && !parents_[old]) parents_[old] = next_++;
      current_[x] = parents_[old];
    }
    for (int id = 1; id < next_; ++id) parents_[id] = id;
    std::swap(previous_, current_);
  }
  int total() const { return total_; }
};

struct Topology { int components; int holes; };
inline Topology topology(RowSampler& sampler, int w, int h, bool reconstructed, uint16_t* memory) {
  const int rw = w + 2;
  Components fg(rw, true, memory), bg(rw, false, memory + 4 * rw + 1);
  for (int y = -1; y <= h; ++y) {
    for (int x = -1; x <= w; ++x) {
      const bool filled = x >= 0 && x < w && y >= 0 && y < h && sampler.ink(x, y, w, h, reconstructed) > 0;
      fg.pixel(x + 1, filled); bg.pixel(x + 1, !filled);
    }
    fg.endRow(); bg.endRow();
  }
  return {fg.total(), bg.total() - 1};
}

inline size_t scratchBytes(int sourceWidth, int destWidth) {
  return 4u * sourceWidth + 16u * (destWidth + 2) + 4;
}

// Allocation failure or unsupported bounds retains the exact baseline path.
// Returns true only when reconstruction passed the component/counter guard.
template <typename Emit>
bool render(Bitmap bitmap, int w, int h, Emit emit, bool forceAllocationFailure = false) {
  if (!bitmap.bytes || bitmap.width < 1 || bitmap.height < 1 || w < 1 || h < 1) return false;
  const bool bounded = (w > bitmap.width || h > bitmap.height) && bitmap.width <= maxWidth && bitmap.height <= maxHeight && w <= maxWidth && h <= maxHeight;
  const size_t bytes = bounded ? scratchBytes(bitmap.width, w) : 0;
  std::unique_ptr<uint16_t[]> memory(bounded && !forceAllocationFailure ? new (std::nothrow) uint16_t[bytes / 2] : nullptr);
  RowSampler sampler(bitmap, reinterpret_cast<int16_t*>(memory.get()));
  bool use = false;
  if (memory) {
    auto* guard = memory.get() + bitmap.width * 2;
    const auto before = topology(sampler, w, h, false, guard);
    const auto after = topology(sampler, w, h, true, guard);
    use = before.components == after.components && before.holes == after.holes;
  }
  for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) emit(x, y, sampler.ink(x, y, w, h, use));
  return use;
}
}  // namespace dropcap_reconstruction
