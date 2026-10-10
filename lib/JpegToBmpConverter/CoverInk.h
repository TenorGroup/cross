#pragma once

#include <IdleWork.h>

#include <cstddef>
#include <cstdint>
#include <string>

#include "ProgressiveLuma.h"

// The sharp tenor/ink cover of a progressive JPEG, made while the device is idle (X3).
//
// Home draws the temporary cover at once (the 1/8 grid JPEGDEC gives). When idle, this step
// decodes the cover's full luma (ProgressiveLuma), runs it through the one tenor/ink kernel
// (CoverStream) and puts the result in place of the temporary file, in the book's cache only:
// the book file is only ever read.
//
// What is known of a book's cover sits in cover.ink beside its other cache files:
//   kind  B baseline or not a JPEG (never rebuilt), P progressive, F cannot be decoded (for good)
//   per frame (width, height, 1 bit or 4 levels; up to 4): sharp, given up, or how many failures
// A failure that may pass (no heap, a read) is not counted; one that may not (the card refused a
// write, the step ran out of time) is, and the third gives that frame up. The file goes with the
// book's cache, as SleepActivity's <cover>.fail does.
namespace coverink {

enum class Kind : uint8_t { Unknown = 0, Baseline = 'B', Progressive = 'P', Failed = 'F' };
enum class Status : uint8_t { Pending = 0, Sharp = 1, GaveUp = 2 };
inline constexpr int kFrames = 4;
inline constexpr int kMaxFailures = 3;

struct Frame {
  uint16_t width = 0, height = 0;
  uint8_t four = 0;
  Status status = Status::Pending;
  uint8_t failures = 0;
  bool is(int w, int h, bool f) const { return width == w && height == h && four == (f ? 1 : 0); }
};

struct Record {
  Kind kind = Kind::Unknown;
  Frame frame[kFrames]{};
  uint8_t count = 0;
  const Frame* find(int w, int h, bool four) const;
  // The frame's entry; a new frame takes the oldest place when all are used.
  Frame& at(int w, int h, bool four);
};

inline std::string statePath(const std::string& cacheDir) { return cacheDir + "/cover.ink"; }
bool read(const std::string& path, Record& out);
bool write(const std::string& path, const Record& record);

enum class Outcome : uint8_t {
  Nothing,  // already sharp, given up, baseline, F, or no temporary cover yet
  Sharp,    // the sharp cover is in place: Home draws it again
  Stopped,  // a key: nothing changed; the next idle moment starts again
  NoHeap,   // the heap floor refused: not counted, try a little later
  Later,    // a read or the copy failed: not counted, not again this wake
  Failed,   // counted against this frame
  Gone,     // the cover cannot be decoded: F
};

// The cover in the book. Each answers Done, Stopped, NoHeap, Io, or Unsupported (not a JPEG).
struct Source {
  // Streams the head of the cover into `sniffer` until it knows the frame.
  progluma::Result (*sniff)(void* user, progluma::Sniffer& sniffer, const idlework::Stop& stop) = nullptr;
  // Copies the whole cover to `path`.
  progluma::Result (*copy)(void* user, const std::string& path, const idlework::Stop& stop) = nullptr;
  void* user = nullptr;
};

// One step for the cover drawn from `inkPath` (tenorink::cachePath) in `cacheDir`.
Outcome build(const std::string& cacheDir, const std::string& inkPath, int width, int height, bool four,
              const Source& source, const idlework::Stop& stop);

// The card files Home saved from a cover (HomeActivity: <thumb>.card plus e, 0 to 3, each with
// its staging and backup names).
void dropCards(const std::string& inkPath);

}  // namespace coverink
