// Scribbled gestures: every sample gesture replayed the way the main loop feeds it, one sample
// a 10 ms pass, and the confusion table printed.
#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "hal/Scribble.h"

namespace {
using namespace scribble;

struct Sample {
  int x, y, dt;
};
struct Gesture {
  std::string file, name, expected;
  int gap = 0;
  std::vector<std::vector<Sample>> strokes;
};

std::vector<Sample> parseStroke(const std::string& s) {
  std::vector<Sample> out;
  std::stringstream ss(s);
  std::string item;
  while (std::getline(ss, item, ';')) {
    Sample p{};
    if (std::sscanf(item.c_str(), "%d,%d,%d", &p.x, &p.y, &p.dt) == 3) out.push_back(p);
  }
  return out;
}

std::vector<Gesture> loadAll() {
  std::vector<Gesture> all;
  std::vector<std::filesystem::path> files;
  for (const auto& e : std::filesystem::directory_iterator(SCRIBBLE_SAMPLES_DIR)) {
    const auto name = e.path().filename().string();
    if (name.rfind("samples", 0) == 0 && e.path().extension() == ".txt") files.push_back(e.path());
  }
  std::sort(files.begin(), files.end());
  for (const auto& f : files) {
    std::ifstream in(f);
    std::string line;
    while (std::getline(in, line)) {
      if (line.empty() || line[0] == '#') continue;
      std::stringstream ss(line);
      Gesture g;
      g.file = f.filename().string();
      ss >> g.name >> g.expected >> g.gap;
      std::string stroke;
      while (ss >> stroke && stroke[0] != '#') g.strokes.push_back(parseStroke(stroke));
      all.push_back(g);
    }
  }
  return all;
}

std::string replay(const Gesture& g, std::vector<Result>* results = nullptr) {
  Scribbler s;
  uint32_t t = 1000;
  std::string out;
  auto feed = [&](bool down, int x, int y) {
    const Result r = s.step(down, x, y, t);
    if (r.kind == Kind::None) return;
    if (!out.empty()) out += "+";
    out += kindName(r.kind);
    if (results) results->push_back(r);
  };
  for (size_t k = 0; k < g.strokes.size(); ++k) {
    if (k > 0) {
      const uint32_t lift = t;
      while (t + 10 < lift + static_cast<uint32_t>(g.gap)) {
        t += 10;
        feed(false, 0, 0);
      }
      t = lift + g.gap;
    }
    for (const Sample& p : g.strokes[k]) {
      t += p.dt;
      feed(true, p.x, p.y);
    }
    t += 10;
    feed(false, 0, 0);
  }
  for (int i = 0; i < 300; ++i) {  // 3 s more: a pending stroke must be decided by then
    t += 10;
    feed(false, 0, 0);
  }
  return out.empty() ? "none" : out;
}

TEST(Scribble, AngleMatchesAtan2) {
  for (int deg = 0; deg < 360; deg += 7) {
    const double r = deg * M_PI / 180.0;
    const int dx = static_cast<int>(std::lround(200 * std::cos(r)));
    const int dy = static_cast<int>(std::lround(200 * std::sin(r)));
    const int got = detail::angle10(dx, dy);
    int diff = std::abs(got - deg * 10);
    if (diff > 1800) diff = 3600 - diff;
    EXPECT_LE(diff, 5) << deg;  // within half a degree
  }
}

TEST(Scribble, IsqrtExact) {
  for (uint32_t v : {0u, 1u, 2u, 3u, 4u, 99u, 100u, 101u, 65535u, 1000000u, 4000000000u}) {
    const uint32_t r = detail::isqrt(v);
    EXPECT_LE(static_cast<uint64_t>(r) * r, v);
    EXPECT_GT(static_cast<uint64_t>(r + 1) * (r + 1), v);
  }
}

TEST(Scribble, LongStrokeKeepsItsEnds) {
  Stroke s;
  s.begin(0, 0, 0);
  for (int i = 1; i <= 1000; ++i) s.add(i * 5, 0, static_cast<uint32_t>(i));
  s.finish();
  EXPECT_LE(s.n, MAX_POINTS);
  EXPECT_EQ(s.p[0].x, 0);
  EXPECT_EQ(s.p[s.n - 1].x, 5000);
  EXPECT_EQ(s.p[s.n - 1].t, 1000);
}

TEST(Scribble, ASwipeIsDecidedOnLift) {
  Scribbler s;
  uint32_t t = 0;
  for (int i = 0; i <= 20; ++i) s.step(true, 100 + i * 15, 400, t += 10);
  const Result r = s.step(false, 0, 0, t += 10);
  EXPECT_EQ(r.kind, Kind::Swipe);  // decided on lift
  EXPECT_LT(r.from.x, r.to.x);
}

TEST(Scribble, AStrikeAimsAtItsMiddle) {
  Scribbler s;
  uint32_t t = 0;
  for (int i = 0; i <= 28; ++i) s.step(true, 100 + i * 10, 300, t += 10);
  for (int i = 1; i <= 28; ++i) s.step(true, 380 - i * 10, 310, t += 10);
  const Result r = s.step(false, 0, 0, t += 10);
  EXPECT_EQ(r.kind, Kind::Strike);
  EXPECT_EQ(r.x, 240);
  EXPECT_EQ(r.y, 305);
}

TEST(Scribble, ASecondFingerSpoilsTheStroke) {
  Scribbler s;
  uint32_t t = 0;
  for (int i = 0; i <= 28; ++i) s.step(true, 100 + i * 10, 300, t += 10);
  s.spoil();
  for (int i = 1; i <= 28; ++i) s.step(true, 380 - i * 10, 310, t += 10);
  EXPECT_EQ(s.step(false, 0, 0, t += 10).kind, Kind::None);
  EXPECT_EQ(s.endedStroke(), nullptr) << "no ink drawn back for the light's stroke";
  for (int i = 0; i <= 20; ++i) s.step(true, 100 + i * 15, 400, t += 10);
  EXPECT_EQ(s.step(false, 0, 0, t += 10).kind, Kind::Swipe) << "the next stroke counts again";
}

TEST(Scribble, Samples) {
  const auto all = loadAll();
  ASSERT_FALSE(all.empty());
  std::map<std::string, std::map<std::string, int>> table;
  std::map<std::string, int> correct, total;
  for (const auto& g : all) {
    const std::string got = replay(g);
    table[g.expected][got]++;
    total[g.file]++;
    if (got == g.expected) correct[g.file]++;
    EXPECT_EQ(got, g.expected) << g.file << " " << g.name;
  }
  std::printf("\nexpected -> got (count)\n");
  for (const auto& [exp, row] : table)
    for (const auto& [got, count] : row) std::printf("  %-12s -> %-12s %d%s\n", exp.c_str(), got.c_str(), count,
                                                     exp == got ? "" : "   <- wrong");
  for (const auto& [file, n] : total) std::printf("%s: %d/%d right\n", file.c_str(), correct[file], n);
}

}  // namespace
