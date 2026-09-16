#pragma once
#include <InflateStream.h>

#include <cstddef>
#include <cstdint>

// The framebuffer is the entire output dictionary. No extra 32 KB window.
inline bool decodeX3BrandPlane(const uint8_t* source, size_t sourceSize, uint8_t* output, size_t outputSize) {
  if (!source || !output || !sourceSize || !outputSize) return false;
  InflateStream stream;
  if (!stream.init(false)) return false;
  stream.setZlibWrapped();
  stream.setSource(source, sourceSize);
  size_t produced = 0;
  return stream.readAtMost(output, outputSize, &produced) == InflateStream::Status::Done && produced == outputSize;
}
