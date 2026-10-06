#include "UglySleepSet.h"

#include <I18n.h>
#include <esp_system.h>

#include <cstdlib>
#include <utility>

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
  const auto s = READING_STATS.habitLedger.summarize(ReadingStatsStore::habitStamp().day);
  c.nightReader = s.activeMs >= HABIT_MIN_MS && s.nightMs * 100ull >= s.activeMs * HABIT_PERCENT;
#ifdef SIMULATOR
  // The simulator test names the day it wants: day,count,hour,minutes,night (a blank keeps the real one).
  if (const char* env = std::getenv("CROSSPOINT_SIM_UGLY_SLEEP")) {
    long v[5];
    bool has[5] = {};
    const char* p = env;
    for (int i = 0; i < 5; ++i) {
      char* end = nullptr;
      v[i] = std::strtol(p, &end, 10);
      has[i] = end != p;
      p = *end == ',' ? end + 1 : end;
    }
    if (has[0]) c.day = static_cast<uint32_t>(v[0]);
    if (has[1]) c.count = static_cast<uint32_t>(v[1]);
    if (has[2]) c.hour = static_cast<int>(v[2]);
    if (has[3]) c.minutesToday = static_cast<int>(v[3]);
    if (has[4]) c.nightReader = v[4] != 0;
  }
#endif
  return c;
}

// A stream inflated into fresh heap, or null (free it). The heap holds it only while the screen is drawn.
uint8_t* unpack(const uint8_t* packed, const size_t packedSize, const size_t raw) {
  auto* out = static_cast<uint8_t*>(std::malloc(raw));
  if (out && !decodeX3BrandPlane(packed, packedSize, out, raw)) std::free(std::exchange(out, nullptr));
  return out;
}

// The sentence of a sleep in the language the screen speaks (Vietnamese, else English), into out.
bool sleepSentence(const Context& c, char* out) {
  const bool vi = I18N.getLanguage() == Language::VI;
  const size_t raw = vi ? sleepdata::TEXT_VI_RAW : sleepdata::TEXT_EN_RAW;
  const uint8_t* text = vi ? unpack(sleepdata::TEXT_VI, sizeof(sleepdata::TEXT_VI), raw) : unpack(sleepdata::TEXT_EN, sizeof(sleepdata::TEXT_EN), raw);
  if (!text) return false;
  const auto* block = reinterpret_cast<const char*>(text);
  const Record r = sleepLine(block, raw, c);
  if (r.text) copy(r, out, SENTENCE_CAP);
  std::free(const_cast<uint8_t*>(text));
  return r.text != nullptr;
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
  uint8_t* pictures = unpack(sleepdata::PICTURES, sizeof(sleepdata::PICTURES), sleepdata::PICTURES_RAW);
  if (!pictures) return false;
  const bool drawn = drawPicture(pictures, sleepdata::PICTURES_RAW, pictureFor(c), ox, oy + PICTURE_TOP,
                                 [&](int x0, int y0, int x1, int y1, int seed, int width) { line(r, x0, y0, x1, y1, seed, width); });
  std::free(pictures);
  char text[SENTENCE_CAP];
  if (!drawn || !sleepSentence(c, text)) return false;
  paragraph(r, Size::S38, 40 + ox, oy + 120, w - 80 - 2 * ox, 58, text);
  return true;
}

bool drawBoot(GfxRenderer& r) {
  const int w = r.getScreenWidth(), h = r.getScreenHeight();
  if (!r.hasFrameBuffer() || w < CANVAS_W || h < BOOT_PICTURE_TOP + CANVAS_H + 160) return false;
  uint8_t* pictures = unpack(sleepdata::PICTURES, sizeof(sleepdata::PICTURES), sleepdata::PICTURES_RAW);
  if (!pictures) return false;
  r.clearScreen();
  const int ox = (w - CANVAS_W) / 2;
  const bool drawn = drawPicture(pictures, sleepdata::PICTURES_RAW, BOOT_PICTURE, ox, BOOT_PICTURE_TOP,
                                 [&](int x0, int y0, int x1, int y1, int seed, int width) { line(r, x0, y0, x1, y1, seed, width); });
  std::free(pictures);
  if (!drawn) return false;
  // The name of the shell over the doodle, underlined by a shaky hand, the line of the day under it.
  const char* name = tr(STR_SHELL_UGLY);
  const int nameWidth = width(r, Size::S38, name);
  text(r, Size::S38, (w - nameWidth) / 2, 88, name);
  underline(r, (w - nameWidth) / 2 - 6, (w + nameWidth) / 2 + 6, 100, 61, 3);
  paragraph(r, Size::S30, 40, BOOT_PICTURE_TOP + CANVAS_H + 44, w - 80, 40, tr(STR_UGLY_BOOT_LINE));
  return true;
}

std::string wakeSentence() {
  const Context c = gather();
  const int line = wakeLineIndex(c);
  return line >= 0 ? I18N.get(WAKE_LINES[line]) : std::string();
}

}  // namespace ugly::sleepset
