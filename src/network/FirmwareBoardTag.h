#pragma once

#include <cstddef>
#include <cstdint>

// Board-identity tag embedded in every CrossPoint image, plus a streaming
// scanner the firmware update paths use to reject an image built for a
// different board before it can boot and drive another device's pins. All the
// S3 boards (sticky, x4pro, papermono, ...) share a chip_id, so the existing
// esp_image_header chip check cannot tell them apart.
//
// The tag is "CROSSPOINT-BOARD-V1:<board>;" stored once in .rodata - the
// scanner's needle references the same array, so a CrossPoint image contains
// exactly one occurrence. Images without a tag (other projects, forks, older
// releases) are allowed: the guard only rejects a tag naming a DIFFERENT
// board.
//
// A board built with TENOR_CABLE_FLASH (the X4 Pro, whose USB download mode is
// fused off) can only be reflashed over the cable by firmware that carries the
// cable channel. Its tag then ends in "TENOR-CABLE-V1:1;", and the cable session and the OTA refuse an
// image whose own tag lacks that field (dropsCable below). The card takes it: that is the way to any other
// firmware, which updates from the card too.

#ifndef TENOR_CABLE_FLASH
#define TENOR_CABLE_FLASH 0
#endif

namespace board_tag {

// Full tag: magic prefix + board name + ';'.
extern const char TAG[];

// Board name of the running firmware (pointer into TAG; not null-terminated at
// the name boundary - always pair with boardNameLen()).
const char* boardName();
size_t boardNameLen();

// Incremental scanner: feed every byte of a candidate image in stream order,
// then check mismatch(). State persists across feed() calls, so chunk
// boundaries splitting the tag are handled.
class Scanner {
 public:
  void feed(const uint8_t* data, size_t len);
  // True once a tag naming a different board has been seen. Valid mid-stream:
  // callers may abort a download as soon as this turns true.
  bool mismatch() const { return mismatchFound; }
  bool matched() const { return matchingTagFound && !mismatchFound; }
  // Board name from the offending tag, for logging (empty until mismatch()).
  const char* foundName() const { return mismatchFound ? captured : ""; }
#if TENOR_CABLE_FLASH
  // The fields that follow the matching board tag, up to its end: they are the image's own, any
  // copy of the same words elsewhere in the image does not count.
  bool cable() const;
  // True when those fields name exactly this product version.
  bool versionIs(const char* version) const;
#endif

 private:
  static constexpr size_t MAX_NAME = 23;
  char captured[MAX_NAME + 1] = {0};
  size_t nameLen = 0;
  size_t magicMatched = 0;
  bool capturing = false;
  bool mismatchFound = false;
  bool matchingTagFound = false;
#if TENOR_CABLE_FLASH
  static constexpr size_t MAX_TAIL = 95;
  char tail[MAX_TAIL + 1] = {0};
  size_t tailLen = 0;
  bool tailOpen = false;
  bool hasField(const char* field, size_t len) const;
#endif
};

// THE rule every update path asks before an image may become the boot image: on a board that
// keeps the cable channel, an image without it (an older release, another project) would leave
// the device with no way to be flashed over the cable again, so it is refused.
#if TENOR_CABLE_FLASH
inline bool dropsCable(const Scanner& image) { return !image.matched() || !image.cable(); }
#else
inline bool dropsCable(const Scanner&) { return false; }
#endif

}  // namespace board_tag
