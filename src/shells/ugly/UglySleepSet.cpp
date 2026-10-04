#include "UglySleepSet.h"

#include <I18n.h>
#include <esp_system.h>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>

#include "ReadingStatsStore.h"
#include "UglyInk.h"
#include "UglySleepData.h"
#include "components/X3BrandCodec.h"

namespace ugly::sleepset {
namespace {
// Survives deep sleep while the board has power: how many times the doodle has been drawn.
constexpr uint32_t COUNT_MAGIC = 0x75676c7a;
RTC_NOINIT_ATTR uint32_t countMagic;
RTC_NOINIT_ATTR uint32_t sleepCount;

constexpr uint32_t HABIT_MIN_MS = 20 * 60 * 1000;  // under 20 minutes of reading there is no habit to name
constexpr uint64_t HABIT_PERCENT = 40;

Context gather() {
  Context c;
  c.day = ReadingStatsStore::currentDay();
  const uint16_t minute = ReadingStatsStore::currentMinute();
  if (minute != ReadingStatsStore::NO_MINUTE) c.hour = minute / 60;
  if (c.day != 0 && READING_STATS.statisticsReadable) {
    const auto& days = READING_STATS.kho.cacNgay();
    c.minutesToday = !days.empty() && days.back().ma == c.day ? static_cast<int>(days.back().phut) : 0;
  }
  if (!READING_STATS.activeBookPath.empty()) c.percent = READING_STATS.activeBook.progress;
  const auto s = READING_STATS.habitLedger.summarize(ReadingStatsStore::habitStamp().day);
  c.nightReader = s.activeMs >= HABIT_MIN_MS && s.nightMs * 100ull >= s.activeMs * HABIT_PERCENT;
  c.earlyReader = s.activeMs >= HABIT_MIN_MS && s.earlyMs * 100ull >= s.activeMs * HABIT_PERCENT;
#ifdef SIMULATOR
  // The simulator test names the day it wants: day,count,hour,minutes,percent,night,early (a blank keeps the real one).
  if (const char* env = std::getenv("CROSSPOINT_SIM_UGLY_SLEEP")) {
    long v[7];
    bool has[7] = {};
    const char* p = env;
    for (int i = 0; i < 7; ++i) {
      char* end = nullptr;
      v[i] = std::strtol(p, &end, 10);
      has[i] = end != p;
      p = *end == ',' ? end + 1 : end;
    }
    if (has[0]) c.day = static_cast<uint32_t>(v[0]);
    if (has[1]) c.count = static_cast<uint32_t>(v[1]);
    if (has[2]) c.hour = static_cast<int>(v[2]);
    if (has[3]) c.minutesToday = static_cast<int>(v[3]);
    if (has[4]) c.percent = static_cast<int>(v[4]);
    if (has[5]) c.nightReader = v[5] != 0;
    if (has[6]) c.earlyReader = v[6] != 0;
  }
#endif
  return c;
}

// The text of the language the screen speaks: Vietnamese, or English for everyone else.
bool inflateText(std::unique_ptr<uint8_t[]>& out, size_t& size) {
  const bool vi = I18N.getLanguage() == Language::VI;
  const uint8_t* packed = vi ? sleepdata::TEXT_VI : sleepdata::TEXT_EN;
  const size_t packedSize = vi ? sizeof(sleepdata::TEXT_VI) : sizeof(sleepdata::TEXT_EN);
  size = vi ? sleepdata::TEXT_VI_RAW : sleepdata::TEXT_EN_RAW;
  out.reset(new (std::nothrow) uint8_t[size]);
  return out && decodeX3BrandPlane(packed, packedSize, out.get(), size);
}
}  // namespace

bool drawScreen(GfxRenderer& r) {
  const int w = r.getScreenWidth(), h = r.getScreenHeight();
  if (!r.hasFrameBuffer() || w < CANVAS_W || h < PICTURE_TOP + CANVAS_H) return false;
  if (countMagic != COUNT_MAGIC) countMagic = COUNT_MAGIC, sleepCount = 0;
  Context c = gather();
  if (c.count == 0) c.count = sleepCount;
  ++sleepCount;
  const int ox = (w - CANVAS_W) / 2, oy = (h - (PICTURE_TOP + CANVAS_H) - 10) / 2 + 10;  // 10 px above, 10 px to spare
  ensureFonts(r);
  r.clearScreen();
  {
    std::unique_ptr<uint8_t[]> pictures(new (std::nothrow) uint8_t[sleepdata::PICTURES_RAW]);
    if (!pictures || !decodeX3BrandPlane(sleepdata::PICTURES, sizeof(sleepdata::PICTURES), pictures.get(), sleepdata::PICTURES_RAW))
      return false;
    if (!drawPicture(pictures.get(), sleepdata::PICTURES_RAW, pictureFor(c), ox, oy + PICTURE_TOP,
                     [&](int x0, int y0, int x1, int y1, int seed, int width) {
                       line(r, x0, y0, x1, y1, static_cast<uint32_t>(seed), width);
                     }))
      return false;
  }
  std::unique_ptr<uint8_t[]> text;
  size_t size = 0;
  if (!inflateText(text, size)) return false;
  const std::string sentence = fill(sleepLine(reinterpret_cast<const char*>(text.get()), size, c), c.percent);
  paragraph(r, Size::S38, 40 + ox, oy + 120, w - 80 - 2 * ox, 58, sentence.c_str());
  return true;
}

std::string wakeSentence() {
  std::unique_ptr<uint8_t[]> text;
  size_t size = 0;
  const Context c = gather();
  if (c.hour < 0 || !inflateText(text, size)) return {};
  return fill(wakeLine(reinterpret_cast<const char*>(text.get()), size, c), c.percent);
}

}  // namespace ugly::sleepset
