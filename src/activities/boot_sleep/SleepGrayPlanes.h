#pragma once

#include <cstdint>
#include <memory>

class GfxRenderer;

// The panel steps of a gray sleep picture: the overlay base, the LSB plane, the MSB plane.
// Without `fold` they go to the panel as before. With `fold` (X3 sleep) no gray waveform runs:
// the planes are folded into one ordered-dither B/W frame, shown by a single FULL_REFRESH. X3
// cuts the panel's power in deep sleep, and gray levels left by one gray pass drift over the
// hours into a dark smear, while black and white from a full GC refresh hold for days.
class SleepGrayPlanes {
 public:
  // The one place that decides it: X3 with "Black and white refresh before sleep" on. Decided
  // once per sleep in SleepActivity::onEnter: reading the settings store in every render path
  // costs flash for its guarded construction each time.
  static void decide(bool x3, uint8_t setting) { on = x3 && setting; }
  static bool wanted() { return on; }
  SleepGrayPlanes(GfxRenderer& renderer, bool fold) : renderer(renderer), fold(fold) {}
  ~SleepGrayPlanes();  // out of line: one copy of the seven frees, not one per caller
  // Overlay (nudge) pictures, once the B/W frame is drawn: stands for displayGrayscaleBase(HALF).
  void base();
  // The LSB plane is in the framebuffer: stands for copyGrayscaleLsbBuffers().
  void lsb();
  // The MSB plane is in the framebuffer: stands for copyGrayscaleMsbBuffers() + displayGrayBuffer().
  void show();

 private:
  // 8 KB pieces: at sleep the heap often has 52 KB free but not in one block.
  static constexpr int CHUNK_BITS = 13;
  static constexpr int CHUNKS = 7;
  GfxRenderer& renderer;
  const bool fold;
  bool overlay = false;
  bool shown = false;
  static inline bool on = false;
  std::unique_ptr<uint8_t[]> kept[CHUNKS];
  bool keep();
};
