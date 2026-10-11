#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

class HalFile;

// The luma of a progressive JPEG at its full grid, without PSRAM (X3). Every scan that carries
// luma gets its own reader with a sector buffer; the readers walk the blocks in step, so only one
// block's coefficients are held at a time (no whole-image coefficient buffer). Rows go out one
// at a time, top to bottom, every row of the image, to the tenor/ink kernel (CoverStream).
//
// Every byte the file gives is checked before it is used: a damaged or cut file returns Corrupt
// and nothing is read or written outside the file and the buffers.
namespace progluma {

constexpr int kMaxWidth = 2048, kMaxHeight = 3072;  // JpegToBmpConverter's limits
constexpr int kMaxReaders = 8;                       // luma scans; more is refused for good
constexpr int kMaxScans = 64;

enum class Kind : uint8_t { Baseline, Progressive, Unsupported, Corrupt };

// The frame header, found by following each segment's length to the SOF (an ICC or EXIF segment
// can push it far from the start). Fed the head of a JPEG in pieces: a zip stream or a file.
class Sniffer {
 public:
  // Takes bytes until the answer is known; true once it is (the rest need not be fed).
  bool feed(const uint8_t* data, size_t size);
  bool known() const { return state == State::Known; }
  // Corrupt when the bytes ran out before an answer.
  Kind kind() const { return known() ? result : Kind::Corrupt; }
  int width() const { return frameWidth; }
  int height() const { return frameHeight; }

 private:
  enum class State : uint8_t { Start0, Start1, Fill, Marker, Len0, Len1, Skip, Frame, Known };
  State state = State::Start0;
  Kind result = Kind::Corrupt;
  uint8_t marker = 0;
  uint32_t left = 0;
  uint8_t head[6]{};
  uint8_t got = 0;
  int frameWidth = 0, frameHeight = 0;
  void decide(Kind kind);
};

// The most a decoder of an image this wide may hold (every reader and table it may take), and its
// biggest block (the band of 8 pixel rows).
size_t worstBytes(int width);
size_t biggestBlock(int width);

enum class Result : uint8_t {
  Done,
  Stopped,      // the caller's stop answered yes; nothing was kept
  NoHeap,       // the heap floor door refused; try again later
  Corrupt,      // damaged or cut file: for good
  Unsupported,  // CMYK, too large, too many scans, chroma wider than luma: for good
  Io,           // a read failed or the row sink refused: try again later
};

struct Sink {
  bool (*row)(void* user, const uint8_t* pixels, int y) = nullptr;
  bool (*stop)(void* user) = nullptr;
  void* user = nullptr;
};

class Decoder {
 public:
  Decoder();
  ~Decoder();
  // Reads every marker of the file once (asking `sink.stop` before every sector), then takes its
  // memory through the heap floor door.
  Result open(HalFile& file, const Sink& sink = Sink{});
  int width() const;
  int height() const;
  uint32_t blocks() const;  // luma blocks decoded so far
  // Decodes and hands out every row. Asks `stop` every 16 blocks and before every sector read.
  Result run(const Sink& sink);
  struct State;

 private:
  std::unique_ptr<State> s;
};

}  // namespace progluma
