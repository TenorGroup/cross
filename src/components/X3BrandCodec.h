#pragma once
#include <InflateStream.h>
#if FREEINK_DEVICE_X4PRO
#include <InflateReader.h>
#include <MinizConfig.h>
#endif

#include <cstddef>
#include <cstdint>

// The framebuffer is the entire output dictionary. No extra 32 KB window.
inline bool decodeX3BrandPlane(const uint8_t* source, size_t sourceSize, uint8_t* output, size_t outputSize,
                               bool compact = false) {
  if (!source || !output || !sourceSize || !outputSize) return false;
#if FREEINK_DEVICE_X4PRO
  if (compact) {
    // One-shot uzlib keeps its small state on the stack and uses the framebuffer as its dictionary.
    static_assert(sizeof(InflateReader) < 4096);
    if (sourceSize < 6 || source[0] != 0x78 || (source[1] & 0x20) ||
        ((static_cast<unsigned>(source[0]) << 8) | source[1]) % 31) return false;
    InflateReader reader;
    if (!reader.init(false)) return false;
    reader.setSource(source, sourceSize);
    reader.skipZlibHeader();
    if (!reader.read(output, outputSize)) return false;
    const uint8_t* checksum = source + sourceSize - 4;
    const uint32_t expected = (uint32_t{checksum[0]} << 24) | (uint32_t{checksum[1]} << 16) |
                              (uint32_t{checksum[2]} << 8) | checksum[3];
    return mz_adler32(1, output, outputSize) == expected;
  }
#else
  (void)compact;
#endif
  InflateStream stream;
  if (!stream.init(false)) return false;
  stream.setZlibWrapped();
  stream.setSource(source, sourceSize);
  size_t produced = 0;
  return stream.readAtMost(output, outputSize, &produced) == InflateStream::Status::Done && produced == outputSize;
}
