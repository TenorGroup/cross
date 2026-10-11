static GfxRenderer renderer;
static int checks = 0, failures = 0;
void check(bool condition, const char* name) {
  ++checks;
  if (!condition) { ++failures; printf("FAIL %s\n", name); }
}
std::string pixels() { return {reinterpret_cast<const char*>(renderer.pixels), sizeof(renderer.pixels)}; }
void clear() { memset(renderer.pixels, 0, sizeof(renderer.pixels)); renderer.outside = 0; ugly::segmentCount = 0; }
bool contained(int left, int top, int right, int bottom) {
  for (int row = 0; row < renderer.screenHeight; ++row)
    for (int col = 0; col < renderer.screenWidth; ++col)
      if (renderer.pixels[row * renderer.screenWidth + col] && (col < left || col >= right || row < top || row >= bottom)) return false;
  return true;
}
int sharpTurns(int count = 75) {
  int turns = 0;
  int previousX = 0, previousY = 0;
  int anchorX = ugly::segments[0].x0, anchorY = ugly::segments[0].y0;
  for (int index = 0; index < std::min(count, ugly::segmentCount); ++index) {
    const auto& segment = ugly::segments[index];
    const int deltaX = segment.x1 - anchorX, deltaY = segment.y1 - anchorY;
    if (deltaX * deltaX + deltaY * deltaY < 16) continue;
    const int64_t dot = deltaX * previousX + deltaY * previousY;
    const int64_t cross = deltaX * previousY - deltaY * previousX;
    if ((previousX || previousY) && (dot <= 0 || 3 * cross * cross > dot * dot)) ++turns;
    previousX = deltaX;
    previousY = deltaY;
    anchorX = segment.x1;
    anchorY = segment.y1;
  }
  return turns;
}
std::array<int, 5> envelope() {
  int left = 528, right = 0, top = 792, bottom = 0;
  int64_t sumX = 0, sumY = 0, sumXX = 0, sumXY = 0;
  const int count = std::min(74, ugly::segmentCount);
  for (int index = 0; index < count; ++index) {
    const auto& segment = ugly::segments[index];
    left = std::min(left, segment.x0);
    right = std::max(right, segment.x0);
    top = std::min(top, segment.y0);
    bottom = std::max(bottom, segment.y0);
    sumX += segment.x0; sumY += segment.y0;
    sumXX += segment.x0 * segment.x0; sumXY += segment.x0 * segment.y0;
  }
  const int64_t variance = count * sumXX - sumX * sumX;
  const int slope = variance ? 256 * (count * sumXY - sumX * sumY) / variance : 0;
  return {(right - left) / 2, (bottom - top) / 2, (left + right) / 2, (top + bottom) / 2, slope};
}
// Circles may touch nearby text: the hand-drawn look was accepted as it is on 10/10/2026.
// Lock that look: any change to the circle shape changes this hash.
uint64_t currentLook() {
  using namespace ugly;
  uint64_t hash = 1469598103934665603ull;
  for (auto role : {Circle::Row, Circle::Word, Circle::Object})
    for (uint32_t seed = 1; seed <= 8; ++seed) {
      clear();
      seededCircle(renderer, role, {48, 80, 380, 116}, 10, 7, seed, 3);
      for (int index = 0; index < renderer.screenWidth * renderer.screenHeight; ++index)
        hash = (hash ^ renderer.pixels[index]) * 1099511628211ull;
    }
  return hash;
}
void tailLength() {
#ifdef UGLY_MARK_SEED_H
  using namespace ugly;
  std::array<uint32_t, 40> seeds{};
  resetMarkSeed();
  for (int index = 0; index < 20; ++index) { seeds[index] = index + 1; seeds[index + 20] = markSeed(); }
  for (auto role : {Circle::Row, Circle::Word, Circle::Object}) {
    clear();
    baseCircle(renderer, role, {48, 80, 380, 116}, 10, 7, 3);
    std::array<Segment, 84> original{};
    std::copy_n(segments, 84, original.begin());
    double baseTail = 0;
    for (int index = 74; index < 84; ++index)
      baseTail += std::hypot(original[index].x1 - original[index].x0, original[index].y1 - original[index].y0);
    auto normalize = [](const Segment* path, int count) {
      std::array<std::array<double, 2>, 84> points{};
      int left = 528, right = 0;
      double sumX = 0, sumY = 0, sumXX = 0, sumXY = 0;
      const int core = std::min(74, count);
      for (int index = 0; index < core; ++index) {
        sumX += path[index].x1; sumY += path[index].y1;
        sumXX += path[index].x1 * path[index].x1; sumXY += path[index].x1 * path[index].y1;
      }
      const double variance = core * sumXX - sumX * sumX;
      const double slope = variance ? (core * sumXY - sumX * sumY) / variance : 0;
      double low = 1e9, high = -1e9;
      for (int index = 0; index < count; ++index) {
        left = std::min(left, path[index].x1); right = std::max(right, path[index].x1);
        const double y = path[index].y1 - slope * path[index].x1;
        low = std::min(low, y); high = std::max(high, y);
      }
      for (int index = 0; index < count; ++index)
        points[index] = {double(path[index].x1 - left) / std::max(1, right - left),
                         (path[index].y1 - slope * path[index].x1 - low) / std::max(1.0, high - low)};
      return points;
    };
    const auto baseShape = normalize(original.data(), 84);
    auto overlapAngle = [](const auto& points, int count) {
      double angle = 0;
      for (int index = 1; index < count; ++index) {
        const double beforeX = points[index - 1][0] - 0.5, beforeY = points[index - 1][1] - 0.5;
        const double afterX = points[index][0] - 0.5, afterY = points[index][1] - 0.5;
        angle += std::atan2(beforeX * afterY - beforeY * afterX, beforeX * afterX + beforeY * afterY);
      }
      return std::abs(angle) - 2 * std::acos(-1.0);
    };
    const double baseAngle = overlapAngle(baseShape, 84);
    double worstDistance = 0, longestTail = 0;
    for (uint32_t seed : seeds) {
      clear();
      seededCircle(renderer, role, {48, 80, 380, 116}, 10, 7, seed, 3);
      double tail = 0;
      for (int index = 74; index < segmentCount; ++index)
        tail += std::hypot(segments[index].x1 - segments[index].x0, segments[index].y1 - segments[index].y0);
      const auto shape = normalize(segments, segmentCount);
      double distance = 0;
      for (int direction = 0; direction < 2; ++direction) {
        const auto& from = direction ? shape : baseShape;
        const auto& to = direction ? baseShape : shape;
        const int fromCount = direction ? segmentCount : 84, toCount = direction ? 84 : segmentCount;
        for (int index = 0; index < fromCount; ++index) {
          double nearest = 1e9;
          for (int other = 0; other < toCount; ++other) {
            const int next = (other + 1) % toCount;
            const double dx = to[next][0] - to[other][0], dy = to[next][1] - to[other][1];
            const double square = dx * dx + dy * dy;
            const double fraction = square ? std::clamp(((from[index][0] - to[other][0]) * dx +
                (from[index][1] - to[other][1]) * dy) / square, 0.0, 1.0) : 0;
            nearest = std::min(nearest, std::hypot(from[index][0] - to[other][0] - fraction * dx,
                                                 from[index][1] - to[other][1] - fraction * dy));
          }
          distance = std::max(distance, nearest);
        }
      }
      check(distance <= 0.08, "normalized Hausdorff distance to old circle <= 0.08");
      check(segmentCount >= 80 && segmentCount <= 84, "old 74-segment loop and 6-10 short overlap segments");
      check(tail > 0 && tail <= baseTail * 1.5, "overlap length positive and <= old overlap x 1.5");
      check(overlapAngle(shape, segmentCount) > 0 && overlapAngle(shape, segmentCount) <= baseAngle * 1.5,
            "overlap angular sweep <= old overlap x 1.5, no second ring");
      worstDistance = std::max(worstDistance, distance);
      longestTail = std::max(longestTail, tail);
    }
    printf("old-shape role=%d Hausdorff=%.5f/0.08 tail=%.1f/%.1fpx\n", static_cast<int>(role),
           worstDistance, longestTail, baseTail * 1.5);
  }
#endif
}
int main(int argc, char** argv) {
  using namespace ugly;
  tailLength();
  const uint64_t look = currentLook();
  printf("look=%016llx\n", static_cast<unsigned long long>(look));
  check(look == 0x283d18cccb17be7aull, "circle keeps the accepted hand-drawn look");
  const int detectorBefore = allocations;
  void* allocation = countedMalloc(16);
  allocation = countedRealloc(allocation, 32);
  free(allocation);
  allocation = countedCalloc(1, 16);
  free(allocation);
  allocation = ::operator new[](16);
  ::operator delete[](allocation);
  allocation = ::operator new(16, std::align_val_t{16});
  ::operator delete(allocation, std::align_val_t{16});
  check(allocations == detectorBefore + 5, "heap detector covers libc and C++");
  for (auto role : {Circle::Word, Circle::Object, Circle::Row}) {
    std::set<std::string> variants;
    std::set<std::array<int, 5>> envelopes;
    clear();
    baseCircle(renderer, role, {48, 80, 380, 116}, 10, 7, 3);
    const int baseTurns = sharpTurns();
#ifndef UGLY_MARK_SEED_H
    const auto baselinePixels = pixels();
    clear();
    seededCircle(renderer, role, {48, 80, 380, 116}, 10, 7, 1, 3);
    check(baselinePixels == pixels(), "base fixture reproduces original circle pixels");
#endif
    int worstTurns = 0;
    for (uint32_t seed = 1; seed <= 20; ++seed) {
      clear();
      const int before = allocations;
      seededCircle(renderer, role, {48, 80, 380, 116}, 10, 7, seed, 3);
      check(allocations == before, "circle zero heap");
      check(segmentCount <= 84, "circle at most 84 segments");
      const int turns = sharpTurns(74);
      worstTurns = std::max(worstTurns, turns);
      check(turns <= baseTurns + 3, "circle sharp turns <= base + 3 at 60 degrees and 4px sampling");
      envelopes.insert(envelope());
      const auto first = pixels();
      clear();
      seededCircle(renderer, role, {48, 80, 380, 116}, 10, 7, seed, 3);
      check(first == pixels(), "same seed same circle pixels");
      check(contained(14, 59, 415, 138), "circle safe box includes clear text and outward tail");
      variants.insert(first);
    }
    printf("circle role=%d distinct=%zu/20\n", static_cast<int>(role), variants.size());
    check(variants.size() >= 18, "20 seeds at least 18 circle shapes");
    printf("circle role=%d envelopes=%zu/20 sharpTurns=%d base=%d\n", static_cast<int>(role), envelopes.size(), worstTurns, baseTurns);
    check(envelopes.size() >= 18, "20 seeds at least 18 distinct coarse envelopes");
  }
  std::set<std::string> pencils;
  for (uint32_t seed = 1; seed <= 20; ++seed) {
    clear();
    const int before = allocations;
    pencil(renderer, 100, 100, 11, seed);
    check(allocations == before, "pencil zero heap");
    const auto first = pixels();
    clear(); pencil(renderer, 100, 100, 11, seed);
    check(first == pixels(), "same seed same pencil pixels");
    check(contained(77, 77, 124, 124), "pencil safe box");
    pencils.insert(first);
  }
  printf("pencil distinct=%zu/20\n", pencils.size());
  check(pencils.size() >= 18, "20 seeds at least 18 pencil shapes");
  for (uint32_t seed = 1; seed <= 20; ++seed) {
    for (auto role : {Circle::Word, Circle::Object, Circle::Row}) {
      for (const auto& box : {Box{0, 0, 14, 14}, Box{514, 778, 528, 792}, Box{48, 80, 62, 94}, Box{-50, -50, 30, 30}}) {
        clear();
        seededCircle(renderer, role, box, 4, 4, seed, 3);
        check(renderer.outside == 0, "edge circle never writes outside screen");
        check(contained(std::max(0, box.x0 - 34), std::max(0, box.y0 - 21),
                        std::min(528, box.x1 + 35), std::min(792, box.y1 + 22)), "edge circle safe box");
      }
    }
    for (auto center : {std::pair{0, 0}, std::pair{527, 791}, std::pair{100, 100}}) {
      clear();
      pencil(renderer, center.first, center.second, 13, seed);
      check(renderer.outside == 0, "edge pencil never writes outside screen");
      check(contained(std::max(0, center.first - 25), std::max(0, center.second - 25),
                      std::min(528, center.first + 26), std::min(792, center.second + 26)), "edge pencil safe box");
    }
  }
  for (auto geometry : {std::pair{528, 792}, std::pair{792, 528}, std::pair{480, 800}, std::pair{800, 480}}) {
    renderer.screenWidth = geometry.first;
    renderer.screenHeight = geometry.second;
    for (uint32_t seed = 1; seed <= 20; ++seed) {
      for (int width : {1, 2, 3}) {
        clear();
        seededCircle(renderer, Circle::Row, {geometry.first - 14, geometry.second - 14, geometry.first, geometry.second},
                     10, 7, seed, width);
        check(renderer.outside == 0, "4 orientations circle writes stay on screen");
        clear();
        pencil(renderer, geometry.first - 1, geometry.second - 1, 13, seed);
        check(renderer.outside == 0, "4 orientations pencil writes stay on screen");
      }
    }
  }
  renderer.screenWidth = 528;
  renderer.screenHeight = 792;
#ifdef UGLY_MARK_SEED_H
  resetMarkSeed(42);
  MarkState state;
  const int seedBefore = allocations;
  const auto first = state.get(7);
  check(first == state.get(7), "unchanged cursor preserves seed");
  check(allocations == seedBefore, "seed supplier and state zero heap");
  const auto counter = markCounter.load();
  for (int redraw = 0; redraw < 20; ++redraw) {
    clear();
    seededCircle(renderer, Circle::Row, {48, 80, 380, 116}, 10, 7, state.get(7), 3);
  }
  check(markCounter.load() == counter, "20 redraws consume no seed");
  const auto second = state.get(8);
  check(first != second, "moving cursor changes seed");
  check(first != state.get(7), "returning cursor changes seed");
  resetMarkSeed(42);
  check(first == markSeed(), "injected seed replays sequence");
  resetMarkSeed(42);
  std::set<uint32_t> seeds;
  for (int index = 0; index < 20; ++index) seeds.insert(markSeed());
  check(seeds.size() == 20, "seed supplier unique sequence");
  for (auto role : {Circle::Word, Circle::Object, Circle::Row}) {
    clear();
    baseCircle(renderer, role, {48, 80, 380, 116}, 10, 7, 3);
    const int baseTurns = sharpTurns();
    int worstTurns = 0;
    std::set<std::array<int, 5>> envelopes;
    for (auto seed : seeds) {
      clear();
      seededCircle(renderer, role, {48, 80, 380, 116}, 10, 7, seed, 3);
      worstTurns = std::max(worstTurns, sharpTurns(54));
      check(sharpTurns(54) <= baseTurns + 3, "supplier circle sharp turns <= base + 3");
      envelopes.insert(envelope());
    }
    printf("supplier role=%d envelopes=%zu/20 sharpTurns=%d base=%d\n", static_cast<int>(role), envelopes.size(), worstTurns, baseTurns);
    check(envelopes.size() >= 18, "supplier 20 seeds at least 18 coarse envelopes");
  }
  state.reset();
  check(state.get(7) != first, "new selection of same object consumes seed");
  std::set<std::string> smallShapes;
  for (uint32_t seed = 1; seed <= 20; ++seed) {
    clear();
    seededCircle(renderer, Circle::Object, {48, 80, 62, 94}, 0, 0, seed, 3);
    smallShapes.insert(pixels());
  }
  printf("small circle distinct=%zu/20\n", smallShapes.size());
  check(smallShapes.size() >= 18, "small circle at least 18 variants");
#endif
  if (argc > 1) {
    clear();
    renderer.screenHeight = 1120;
#ifdef UGLY_MARK_SEED_H
    resetMarkSeed();
#endif
    baseCircle(renderer, Circle::Row, {48, 88, 330, 120}, 10, 7, 3);
    for (uint32_t index = 0; index < 6; ++index) {
      const int baseline = 180 + index * 88;
#ifdef UGLY_MARK_SEED_H
      const auto circleSeed = markSeed(), pencilSeed = markSeed();
#else
      const auto circleSeed = index + 1, pencilSeed = index + 1;
#endif
      seededCircle(renderer, Circle::Row, {48, baseline, 330, baseline + 32}, 10, 7, circleSeed, 3);
      seededCircle(renderer, Circle::Word, {421, baseline + 5, 443, baseline + 27}, 0, 0, pencilSeed, 2);
      pencil(renderer, 432, baseline + 16, 11, pencilSeed);
      printf("sample=%u circleSeed=%u pencilSeed=%u\n", index, circleSeed, pencilSeed);
    }
    for (int fixture = 1; fixture < 3; ++fixture) {
      for (int column = 0; column < 3; ++column) {
        const int x = 50 + column * 165, y = 770 + (fixture - 1) * 105;
        seededCircle(renderer, static_cast<Circle>(column), {x, y, x + textFixtures[fixture].width,
                     y + textFixtures[fixture].height}, 10, 7, 12 + column + fixture * 3, 3);
      }
    }
    std::string path = std::string(argv[1]) + "/marks.pgm";
    FILE* file = fopen(path.c_str(), "wb");
    if (!file) return 2;
    fprintf(file, "P5\n528 1120\n255\n");
    for (auto pixel : renderer.pixels) fputc(pixel ? 0 : 255, file);
    fclose(file);
  }
  if (argc > 2 && !strcmp(argv[2], "--benchmark")) {
    constexpr int samples = 2000;
    const int before = allocations;
    auto start = std::chrono::steady_clock::now();
    for (int sample = 0; sample < samples; ++sample) {
      seededCircle(renderer, Circle::Row, {48, 80, 380, 116}, 10, 7, sample + 1, 3);
      std::atomic_signal_fence(std::memory_order_seq_cst);
    }
    const double circleUs = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() / samples;
    start = std::chrono::steady_clock::now();
    for (int sample = 0; sample < samples; ++sample) {
      pencil(renderer, 100, 100, 11, sample + 1);
      std::atomic_signal_fence(std::memory_order_seq_cst);
    }
    const double pencilUs = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() / samples;
    check(allocations == before, "benchmark marks zero heap");
    printf("host ASan/UBSan samples=%d circle=%.3fus pencil=%.3fus\n", samples, circleUs, pencilUs);
  }
  printf("%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
