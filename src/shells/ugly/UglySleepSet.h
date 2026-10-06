#pragma once
// The sleep set of the tenor/ugly shell: which doodle and which sentence a sleep or a wake shows, and how
// a doodle is rebuilt from its strokes. Integers only and no hardware, so a host test runs it as it is.
// The data is baked by scripts/ugly/gen_sleep_set.py (its header lists the stream layout).
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>

#include <I18nKeys.h>

#include "UglyLogic.h"

class GfxRenderer;

namespace ugly::sleepset {

inline constexpr int PICS = 8;
inline constexpr int CANVAS_W = 480, CANVAS_H = 440;  // a doodle is drawn in this box
inline constexpr int PICTURE_TOP = 330;               // its top, under up to 4 lines of the sentence
inline constexpr int STEP = 40, AMP = 2;              // a wobbling point per STEP px of a long segment
inline constexpr int MAX_POINTS = 256;                // points of a stroke after the wobble is added

// What the day knows when the screen is drawn. A field the clock or the statistics cannot name stays at -1 or 0.
struct Context {
  uint32_t day = 0;     // yyyymmdd, 0 when the clock is unknown
  uint32_t count = 0;   // sleeps since the board had power
  int hour = -1;        // 0-23
  int minutesToday = -1;  // minutes read today, -1 when unknown
  bool nightReader = false;
};

// Line codes in the text stream (see gen_sleep_set.py): a generic sleep line, b nothing read today,
// d night reader, f..l the hour of going to sleep.
inline int sleepBand(const int hour) {
  static constexpr int8_t TOP[7] = {5, 9, 12, 14, 18, 22, 24};
  for (int i = 0; i < 7; ++i)
    if (hour < TOP[i]) return i;
  return 6;
}
inline int wakeBand(const int hour) {
  static constexpr int8_t TOP[9] = {5, 7, 9, 11, 13, 17, 19, 22, 24};
  for (int i = 0; i < 9; ++i)
    if (hour < TOP[i]) return i;
  return 8;
}

// The doodle a day and a sleep count pick. Consecutive sleeps walk all 8.
inline uint32_t rotation(const Context& c) { return c.day == 0 ? c.count : static_cast<uint32_t>(logic::civilDays(c.day)) + c.count; }
inline int pictureFor(const Context& c) { return static_cast<int>(rotation(c) % PICS); }

inline constexpr uint32_t NO_LINE = 0xFFFFFFFF;  // "shown last" before anything was shown

struct Record {
  const char* text = nullptr;  // not terminated: len bytes
  int len = 0;
  uint32_t id = NO_LINE;  // which record of the stream (the same in both languages)
};

inline int countCode(const char* block, const size_t size, const char code) {
  int n = 0;
  for (size_t i = 0; i < size; ++i)
    if (block[i] == code && (i == 0 || block[i - 1] == '\n')) ++n;
  return n;
}

// The index-th record of a code, without its code byte and its newline.
inline Record recordAt(const char* block, const size_t size, const char code, int index) {
  size_t i = 0;
  for (uint32_t id = 0; i < size; ++id) {
    size_t end = i;
    while (end < size && block[end] != '\n') ++end;
    if (block[i] == code && index-- == 0) return {block + i + 1, static_cast<int>(end - i - 1), id};
    i = end + 1;
  }
  return {};
}

// Picks one record out of the codes in `codes`, spread evenly over them by `select`.
inline Record pickFrom(const char* block, const size_t size, const char* codes, const int nCodes, const uint32_t select) {
  int total = 0;
  for (int i = 0; i < nCodes; ++i) total += countCode(block, size, codes[i]);
  if (total == 0) return {};
  int at = static_cast<int>(select % static_cast<uint32_t>(total));
  for (int i = 0; i < nCodes; ++i) {
    const int n = countCode(block, size, codes[i]);
    if (at < n) return recordAt(block, size, codes[i], at);
    at -= n;
  }
  return {};
}

// The sentence of a sleep: one pool of the lines that know something of the day (the hour, nothing read, the
// night habit) and the lines for anybody, taking turns by the sleep count; never `last`, the id shown last.
inline Record sleepLine(const char* block, const size_t size, const Context& c, const uint32_t last = NO_LINE) {
  char codes[4];
  int n = 0;
  if (c.hour >= 0) codes[n++] = static_cast<char>('f' + sleepBand(c.hour));
  if (c.day != 0 && c.minutesToday == 0) codes[n++] = 'b';
  if (c.nightReader) codes[n++] = 'd';
  codes[n++] = 'a';
  int total = 0;
  for (int i = 0; i < n; ++i) total += countCode(block, size, codes[i]);
  const auto at = [&](const int i) { return pickFrom(block, size, codes, n, static_cast<uint32_t>(i)); };
  const int i = logic::takeTurn(rotation(c), total, last, [&](const int k) { return at(k).id; });
  return i < 0 ? Record{} : at(i);
}

inline constexpr StrId WAKE_LINES[] = {
    StrId::STR_UGLY_WAKE_M1, StrId::STR_UGLY_WAKE_M2, StrId::STR_UGLY_WAKE_N1, StrId::STR_UGLY_WAKE_N2,
    StrId::STR_UGLY_WAKE_O1, StrId::STR_UGLY_WAKE_O2, StrId::STR_UGLY_WAKE_P1, StrId::STR_UGLY_WAKE_P2,
    StrId::STR_UGLY_WAKE_Q1, StrId::STR_UGLY_WAKE_Q2, StrId::STR_UGLY_WAKE_R1, StrId::STR_UGLY_WAKE_R2,
    StrId::STR_UGLY_WAKE_S1, StrId::STR_UGLY_WAKE_S2, StrId::STR_UGLY_WAKE_S3, StrId::STR_UGLY_WAKE_T1,
    StrId::STR_UGLY_WAKE_T2, StrId::STR_UGLY_WAKE_U1, StrId::STR_UGLY_WAKE_U2,
};

// The i18n wake line: the lines of the hour take turns by the sleep count, never `last` (the index shown last).
// -1 when the clock does not know the hour.
inline int wakeLineIndex(const Context& c, const uint32_t last = NO_LINE) {
  if (c.hour < 0) return -1;
  static constexpr uint8_t FIRST[9] = {0, 2, 4, 6, 8, 10, 12, 15, 17};
  static constexpr uint8_t COUNT[9] = {2, 2, 2, 2, 2, 2, 3, 2, 2};
  const int band = wakeBand(c.hour);
  const auto id = [&](const int i) { return static_cast<uint32_t>(FIRST[band] + i); };
  return FIRST[band] + logic::takeTurn(rotation(c), COUNT[band], last, id);
}

// What the shell keeps through deep sleep while the board has power (RTC_NOINIT under one magic, checked on every
// use): the sleep count, and the line each picker showed last, so a wake neither starts the lines over nor
// repeats the one before. Trivial on purpose: an initializer would wipe it on every boot.
struct Kept {
  uint32_t magic;
  uint32_t sleepCount;
  uint32_t lastSleep;  // Record::id
  uint32_t lastWake;   // index into WAKE_LINES
  uint8_t quipTurns[16];  // by Quip event: how many times it has spoken
  uint16_t lastQuip[16];  // by Quip event: the line it said last
};
Kept& kept();

// The record as a sentence in `out` (cap bytes, cut to fit, always ended). Returns out.
inline constexpr size_t SENTENCE_CAP = 128;
inline char* copy(const Record& r, char* out, const size_t cap) {
  const size_t n = static_cast<size_t>(r.len) < cap - 1 ? static_cast<size_t>(r.len) : cap - 1;
  for (size_t i = 0; i < n; ++i) out[i] = r.text[i];
  out[n] = 0;
  return out;
}

// Rebuilds one doodle: the strokes of picture `pic` as pen segments. line(x0, y0, x1, y1, seed, width) draws
// one; (ox, oy) moves the canvas onto the screen. False when the stream is cut or has no such picture.
template <class Line>
bool drawPicture(const uint8_t* data, const size_t size, const int pic, const int ox, const int oy, Line line) {
  size_t at = 0;
  if (size < 1 || pic < 0 || pic >= data[0]) return false;
  at = 1;
  for (int p = 0;; ++p) {
    if (at >= size) return false;
    const int strokes = data[at++];
    for (int s = 0; s < strokes; ++s) {
      if (at + 4 > size) return false;
      const int n = data[at], width = data[at + 1], seed = (p * 7 + s * 3 + 1) % 64;
      int x = 2 * data[at + 2], y = 2 * data[at + 3];
      at += 4;
      if (n < 2 || at + 2 * (n - 1) > size) return false;
      if (p != pic) {
        at += 2 * (n - 1);
        continue;
      }
      int16_t ex[MAX_POINTS], ey[MAX_POINTS];
      int m = 0, idx = 0;
      ex[m] = x, ey[m++] = y;
      for (int i = 1; i < n; ++i) {
        const int nx = x + 2 * static_cast<int8_t>(data[at]), ny = y + 2 * static_cast<int8_t>(data[at + 1]);
        at += 2;
        const int dx = nx - x, dy = ny - y;
        const int k = std::max(1, std::max(std::abs(dx), std::abs(dy)) / STEP);
        if (m + k > MAX_POINTS) return false;
        for (int j = 1; j < k; ++j, ++idx) {
          ex[m] = static_cast<int16_t>(x + dx * j / k + logic::wobble(seed, 2 * idx, AMP));
          ey[m++] = static_cast<int16_t>(y + dy * j / k + logic::wobble(seed, 2 * idx + 1, AMP));
        }
        ex[m] = static_cast<int16_t>(nx), ey[m++] = static_cast<int16_t>(ny);
        x = nx, y = ny;
      }
      // Catmull-Rom through the points, 3 steps per segment, in integers (q is 54 times the coordinate).
      int16_t* const axis[2] = {ex, ey};
      int px = ex[0], py = ey[0];
      for (int i = 0; i + 1 < m; ++i) {
        const int i0 = i > 0 ? i - 1 : 0, i3 = i + 2 < m ? i + 2 : m - 1;
        for (int t = 0; t < 3; ++t) {
          int at1[2];
          for (int a = 0; a < 2; ++a) {
            const int16_t* v = axis[a];
            at1[a] = (54 * v[i] + (v[i + 1] - v[i0]) * t * 9 + (2 * v[i0] - 5 * v[i] + 4 * v[i + 1] - v[i3]) * t * t * 3 +
                      (-v[i0] + 3 * v[i] - 3 * v[i + 1] + v[i3]) * t * t * t + 27) / 54;
          }
          if (i > 0 || t > 0) line(ox + px, oy + py, ox + at1[0], oy + at1[1], seed, width);
          px = at1[0], py = at1[1];
        }
      }
      line(ox + px, oy + py, ox + ex[m - 1], oy + ey[m - 1], seed, width);
    }
    if (p == pic) return true;
  }
}

// The two that need the board. drawScreen puts the sleep screen on the frame (cleared first; false when the
// frame is too small or the stream cannot be unpacked, the frame then is not finished). wakeSentence is the line
// a wake shows at the top of the diary, empty when the day has none.
bool drawScreen(GfxRenderer& renderer);
std::string wakeSentence();
// The boot screen of the shell, on the frame (cleared first; false when the frame is too small or the stream cannot be
// unpacked): its name in hand, the dog asleep on the books, a line of abuse. The caller adds the version and shows it.
inline constexpr int BOOT_PICTURE = 2, BOOT_PICTURE_TOP = 116;
bool drawBoot(GfxRenderer& renderer);

}  // namespace ugly::sleepset
