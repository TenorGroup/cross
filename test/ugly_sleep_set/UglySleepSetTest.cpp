// tenor/ugly sleep set: the 8 doodles, the sentences, what picks them, and that every sentence fits.
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include "components/X3BrandCodec.h"
#include "shells/ugly/UglySleepData.h"
#include "shells/ugly/UglySleepSet.h"
#include "shells/ugly/fonts/ugly_22.h"
#include "shells/ugly/fonts/ugly_38.h"

namespace {
using namespace ugly::sleepset;
namespace data = ugly::sleepdata;

template <size_t N>
std::vector<uint8_t> unpack(const uint8_t (&packed)[N], const unsigned raw) {
  std::vector<uint8_t> out(raw);
  EXPECT_TRUE(decodeX3BrandPlane(packed, N, out.data(), raw));
  return out;
}

std::string text(bool vi) {
  const auto raw = vi ? unpack(data::TEXT_VI, data::TEXT_VI_RAW) : unpack(data::TEXT_EN, data::TEXT_EN_RAW);
  return std::string(raw.begin(), raw.end());
}

std::vector<std::pair<char, std::string>> records(const std::string& block) {
  std::vector<std::pair<char, std::string>> out;
  size_t i = 0;
  while (i < block.size()) {
    const size_t end = block.find('\n', i);
    out.push_back({block[i], block.substr(i + 1, end - i - 1)});
    i = end + 1;
  }
  return out;
}

struct Seg {
  int x0, y0, x1, y1, seed, w;
};
std::vector<Seg> draw(int pic, int ox = 0, int oy = 0) {
  static const auto pictures = unpack(data::PICTURES, data::PICTURES_RAW);
  std::vector<Seg> out;
  EXPECT_TRUE(drawPicture(pictures.data(), pictures.size(), pic, ox, oy,
                          [&](int a, int b, int c, int d, int s, int w) { out.push_back({a, b, c, d, s, w}); }));
  return out;
}

uint64_t hashOf(const std::vector<Seg>& segs) {
  uint64_t h = 1469598103934665603ull;
  for (const auto& s : segs)
    for (int v : {s.x0, s.y0, s.x1, s.y1, s.seed, s.w}) h = (h ^ static_cast<uint32_t>(v)) * 1099511628211ull;
  return h;
}

TEST(SleepSet, FoundersLinesAreWordForWord) {
  const std::string vi = text(true);
  for (const char* line : {"Tao ngủ đây, mai nhớ thức tao dậy. Còn giờ thì kệ cmm, zz Z Z",
                           "Được sếp nghỉ cho ăn trưa mới kêu tao dậy chứ gì.", "Toàn đọc lúc nửa đêm, vừa đi ăn trộm vừa đọc à?"})
    EXPECT_NE(vi.find(line), std::string::npos) << line;
}

TEST(SleepSet, BothLanguagesCarryTheSameCodesInTheSameOrder) {
  auto vi = records(text(true));
  const auto en = records(text(false));
  vi.erase(std::remove_if(vi.begin(), vi.end(), [](const auto& r) { return r.first >= 'm'; }), vi.end());  // English has no wake lines
  ASSERT_EQ(vi.size(), en.size());
  for (size_t i = 0; i < vi.size(); ++i) EXPECT_EQ(vi[i].first, en[i].first) << i;
  EXPECT_GE(vi.size(), 15u);
}

TEST(SleepSet, EveryCodeThePickersAskForHasLines) {
  const std::string vi = text(true);
  for (char c : std::string("abdfghijlmnopqrstu")) EXPECT_GE(countCode(vi.data(), vi.size(), c), 1) << c;
  // f held 2 lines until the review of 04/10 cut the one about the face: it keeps the one about the dawn.
  for (char c : std::string("himnopqrtu")) EXPECT_GE(countCode(vi.data(), vi.size(), c), 2) << c;
}

TEST(SleepSet, WritingRulesHold) {
  for (bool vi : {true, false})
    for (const auto& [code, line] : records(text(vi))) {
      for (const char* bad : {"\xe2\x80\x94", "\xe2\x80\x93", "\xc2\xb7"}) EXPECT_EQ(line.find(bad), std::string::npos) << line;
      EXPECT_EQ(line.find("không phải"), std::string::npos) << line;
      if (vi)
        for (const char* w : {" một ", " hai ", " ba ", " bốn ", " sáu ", " bảy ", " tám ", " chín ", " mười "})
          EXPECT_EQ(line.find(w), std::string::npos) << "count in words: " << line;
      EXPECT_EQ(line.find('#'), std::string::npos) << line;
    }
}

TEST(SleepSet, EveryPictureDrawsInsideItsCanvasAndTwiceTheSame) {
  std::set<uint64_t> seen;
  for (int p = 0; p < PICS; ++p) {
    const auto a = draw(p), b = draw(p);
    EXPECT_GT(a.size(), 40u) << p;
    EXPECT_EQ(hashOf(a), hashOf(b)) << "same picture, same pixels: " << p;
    for (const auto& s : a) {
      EXPECT_GE(std::min(s.x0, s.x1), 0) << p;
      EXPECT_LE(std::max(s.x0, s.x1), CANVAS_W) << p;
      EXPECT_GE(std::min(s.y0, s.y1), 0) << p;
      EXPECT_LE(std::max(s.y0, s.y1), CANVAS_H) << p;
      EXPECT_LT(std::max(std::abs(s.x1 - s.x0), std::abs(s.y1 - s.y0)), 60) << "under 60 px, so the pen adds no wobble of its own " << p;
    }
    seen.insert(hashOf(a));
  }
  EXPECT_EQ(seen.size(), static_cast<size_t>(PICS));
  const auto moved = draw(0, 24, 340);
  EXPECT_EQ(moved[0].x0, draw(0)[0].x0 + 24);
  EXPECT_EQ(moved[0].y0, draw(0)[0].y0 + 340);
}

TEST(SleepSet, ACutStreamIsRefusedNotDrawnHalf) {
  static const auto pictures = unpack(data::PICTURES, data::PICTURES_RAW);
  int n = 0;
  const auto pen = [&](int, int, int, int, int, int) { ++n; };
  EXPECT_FALSE(drawPicture(pictures.data(), pictures.size() / 2, PICS - 1, 0, 0, pen));
  EXPECT_FALSE(drawPicture(pictures.data(), pictures.size(), PICS, 0, 0, pen));
}

TEST(SleepSet, DaysPickDifferentPicturesAndTheSameDayTheSame) {
  Context c;
  std::set<int> seen;
  for (uint32_t day : {20261001u, 20261002u, 20261003u, 20261004u}) {
    c.day = day;
    EXPECT_EQ(pictureFor(c), pictureFor(c));
    seen.insert(pictureFor(c));
  }
  EXPECT_EQ(seen.size(), 4u);
  c.day = 20261004;
  const int first = pictureFor(c);
  c.count = 1;
  EXPECT_NE(pictureFor(c), first) << "a second sleep the same day shows another doodle";
  std::set<int> month;
  for (uint32_t d = 20261001; d <= 20261008; ++d) c.day = d, c.count = 0, month.insert(pictureFor(c));
  EXPECT_EQ(month.size(), static_cast<size_t>(PICS));
}

TEST(SleepSet, SentenceFollowsTheHourOfSleepAndIsStable) {
  const std::string vi = text(true);
  Context c;
  c.day = 20261004;
  c.hour = 13;  // nap after lunch: code i
  std::set<std::string> lunch, generic;
  const auto all = records(vi);
  for (uint32_t n = 0; n < 40; ++n) {
    c.count = n;
    const Record r = sleepLine(vi.data(), vi.size(), c);
    ASSERT_NE(r.text, nullptr);
    const std::string s(r.text, r.len);
    EXPECT_EQ(s, std::string(sleepLine(vi.data(), vi.size(), c).text, r.len));
    char code = 0;
    for (const auto& [k, line] : all)
      if (line == s) code = k;
    EXPECT_TRUE(code == 'i' || code == 'a') << s;
    (code == 'i' ? lunch : generic).insert(s);
  }
  EXPECT_EQ(lunch.size(), 2u) << "both lunch-nap lines show up";
  EXPECT_GE(generic.size(), 5u) << "and the generic ones keep turning";
}

TEST(SleepSet, SleepSentenceKnowsNothingReadTodayAndTheHabit) {
  const std::string vi = text(true);
  const auto all = records(vi);
  auto codeOf = [&](const Record& r) {
    const std::string s(r.text, r.len);
    for (const auto& [k, line] : all)
      if (line == s) return k;
    return '?';
  };
  Context c;
  c.day = 20261004, c.hour = 10, c.minutesToday = 0, c.nightReader = true;
  std::set<char> codes;
  for (uint32_t n = 0; n < 400; ++n) {
    c.count = n;
    codes.insert(codeOf(sleepLine(vi.data(), vi.size(), c)));
  }
  EXPECT_EQ(codes, (std::set<char>{'a', 'b', 'd', 'h'}));
  c = {};  // no clock, no statistics: only generic lines
  for (uint32_t n = 0; n < 30; ++n) {
    c.count = n;
    EXPECT_EQ(codeOf(sleepLine(vi.data(), vi.size(), c)), 'a');
  }
}

TEST(SleepSet, WakeSentenceFollowsTheHourOfWaking) {
  const std::string vi = text(true), en = text(false);
  Context c;
  c.day = 20261004;
  EXPECT_EQ(wakeLine(vi.data(), vi.size(), c).text, nullptr) << "no hour, no greeting";
  c.hour = 11;
  std::set<std::string> lines;
  for (uint32_t d = 20261001; d <= 20261010; ++d) {
    c.day = d;
    const Record r = wakeLine(vi.data(), vi.size(), c);
    ASSERT_NE(r.text, nullptr);
    lines.insert(std::string(r.text, r.len));
    EXPECT_EQ(std::string(r.text, r.len), std::string(wakeLine(vi.data(), vi.size(), c).text, r.len)) << "the same day, the same greeting";
  }
  EXPECT_EQ(lines.size(), 2u) << "the two lines for 11 to 13 o'clock";
  EXPECT_TRUE(lines.count("Được sếp nghỉ cho ăn trưa mới kêu tao dậy chứ gì."));
  c.nightReader = true;
  bool habit = false;
  for (uint32_t d = 20261001; d <= 20261020; ++d) {
    c.day = d;
    const Record r = wakeLine(en.data(), en.size(), c);
    habit |= std::string(r.text, r.len).find("Burgling") != std::string::npos;
  }
  EXPECT_TRUE(habit) << "a night reader gets the habit line some mornings";
}

// The hour of the day to its code, written out here on its own, hour by hour.
TEST(SleepSet, EveryHourMapsToItsBand) {
  const std::string vi = text(true);
  const auto all = records(vi);
  auto codeOf = [&](const Record& r) {
    const std::string s(r.text, r.len);
    for (const auto& [k, line] : all)
      if (line == s) return k;
    return '?';
  };
  const char wakeHours[] = {'m','m','m','m','m','n','n','o','o','p','p','q','q','r','r','r','r','s','s','t','t','t','u','u'};
  const char sleepHours[] = {'f','f','f','f','f','g','g','g','g','h','h','h','i','i','j','j','j','j','k','k','k','k','l','l'};
  for (int h = 0; h < 24; ++h) {
    Context c;
    c.day = 20261004, c.hour = h;
    EXPECT_EQ(codeOf(wakeLine(vi.data(), vi.size(), c)), wakeHours[h]) << "wake at " << h;
    for (uint32_t n = 0; n < 12; ++n) {
      c.count = n;
      const char got = codeOf(sleepLine(vi.data(), vi.size(), c));
      EXPECT_TRUE(got == sleepHours[h] || got == 'a') << "sleep at " << h << " gave " << got;
    }
  }
}

TEST(SleepSet, CopyCutsToFitAndAlwaysEnds) {
  const Record r{"Tao ngu day", 11};
  char out[SENTENCE_CAP];
  EXPECT_STREQ(copy(r, out, sizeof(out)), "Tao ngu day");
  EXPECT_STREQ(copy(r, out, 5), "Tao ");
}

// Every sentence is drawn in the baked font, and fits: 4 lines of 400 px at the sleep size, 2 at the wake size.
int advance(const EpdFontData& f, uint32_t cp) {
  for (uint32_t i = 0; i < f.intervalCount; ++i) {
    const auto& r = f.intervals[i];
    if (cp >= r.first && cp <= r.last) return fp4::toPixel(f.glyph[r.offset + cp - r.first].advanceX) + 1;  // +1: the widest jump
  }
  return -1;
}

int linesOf(const EpdFontData& f, const std::string& s, int maxWidth) {
  std::vector<std::string> words;
  size_t from = 0;
  while (from < s.size()) {
    const size_t blank = s.find(' ', from);
    words.push_back(s.substr(from, blank == std::string::npos ? std::string::npos : blank - from));
    if (blank == std::string::npos) break;
    from = blank + 1;
  }
  std::vector<ugly::logic::Token> tokens;
  for (const auto& w : words) tokens.push_back({w.c_str(), 0, false});
  std::vector<ugly::logic::Placed> placed(tokens.size());
  int bad = 0;
  const int lines = ugly::logic::layout(tokens.data(), static_cast<int>(tokens.size()), maxWidth, advance(f, 'a') / 2 + 4,
                                        [&](const char* t) {
                                          int w = 0;
                                          const auto* p = reinterpret_cast<const unsigned char*>(t);
                                          while (const uint32_t cp = utf8NextCodepoint(&p)) {
                                            const int a = advance(f, cp);
                                            if (a < 0) ++bad;
                                            w += std::max(a, 0);
                                          }
                                          return w;
                                        },
                                        placed.data());
  return bad ? 99 : lines;
}

TEST(SleepSet, TheFitCheckBitesOnALongSentence) {
  EXPECT_GT(linesOf(ugly_38, std::string("a aa aaa aaaa aaaaa aaaaaa aaaaaaa aaaaaaaa aaaaaaaaa aaaaaaaaaa aaaaaaaaaaa aaaaaaaaaaaa"), 400), 4);
  EXPECT_EQ(linesOf(ugly_38, "Tao \xe2\x82\xac", 400), 99) << "a letter the font lacks";
}

TEST(SleepSet, EverySentenceIsInTheBakedFontAndFits) {
  for (bool vi : {true, false})
    for (const auto& [code, raw] : records(text(vi))) {
      const std::string& s = raw;
      const bool wake = code >= 'm';
      if (wake) {
        EXPECT_LE(linesOf(ugly_22, s, 400), 2) << s;
      } else if (code != 'd') {
        EXPECT_LE(linesOf(ugly_38, s, 400), 4) << s;
      } else {
        EXPECT_LE(linesOf(ugly_38, s, 400), 4) << s;
        EXPECT_LE(linesOf(ugly_22, s, 400), 2) << s;
      }
    }
}
}  // namespace
