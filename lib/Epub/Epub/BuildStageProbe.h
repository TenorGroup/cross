#pragma once

// Press probe only: where laying out a chapter spends its time. Each stage adds the microseconds
// spent inside it; stages nest (a parse step holds the reads, the word measuring, the line breaks
// and the line extraction, and extracting a line can complete a page), so buildprobe::log prints
// each stage without the stages inside it.
#ifdef TENOR_PRESS_PROBE
#include <Arduino.h>
#include <Logging.h>

#include <cstdint>

namespace buildprobe {
enum Stage : uint8_t { Tick, Resume, Park, Step, Read, SdFont, Widths, Breaks, Extract, PageWrite, StageCount };
inline uint32_t us[StageCount] = {};
inline uint32_t pages = 0;
// STARVE_BUILD <n>: the n-th heap check of a build step answers "starved" once, so Section parks
// the build and the reader sheds the font caches and resumes, on a heap that is not short at all.
inline uint32_t starveAfterSteps = 0;
struct Scope {
  Stage stage;
  uint32_t started;
  explicit Scope(const Stage s) : stage(s), started(micros()) {}
  ~Scope() { us[stage] += micros() - started; }
};
inline void reset() {
  for (auto& v : us) v = 0;
  pages = 0;
}
inline uint32_t ms(const uint32_t v) { return v / 1000; }
inline uint32_t minus(const uint32_t a, const uint32_t b) { return a > b ? a - b : 0; }
// `where` names the build that just ended: a jump, a look-ahead, an open.
inline void log(const char* where) {
  const uint32_t layout = us[SdFont] + us[Widths] + us[Breaks] + us[Extract];
  LOG_INF("SCT", "BUILD_STAGES src=%s pages=%u tick=%u resume=%u park=%u read=%u xml=%u sdfont=%u widths=%u "
                 "breaks=%u extract=%u page_write=%u",
          where, static_cast<unsigned>(pages), ms(us[Tick]), ms(us[Resume]), ms(us[Park]), ms(us[Read]),
          ms(minus(us[Step], us[Read] + layout)), ms(us[SdFont]), ms(us[Widths]), ms(us[Breaks]),
          ms(minus(us[Extract], us[PageWrite])), ms(us[PageWrite]));
}
}  // namespace buildprobe
#define BUILD_PROBE_SCOPE(stage) const buildprobe::Scope buildProbeScope(buildprobe::stage)
#else
#define BUILD_PROBE_SCOPE(stage)
#endif
