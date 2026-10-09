#pragma once

#include <FontDecompressor.h>
#include <zlib.h>

extern "C" uint32_t uzlib_adler32(const void* data, unsigned int length, uint32_t previous) {
  return static_cast<uint32_t>(adler32(previous, static_cast<const Bytef*>(data), length));
}
extern "C" uint32_t uzlib_crc32(const void* data, unsigned int length, uint32_t previous) {
  return static_cast<uint32_t>(crc32(previous ^ 0xffffffffu, static_cast<const Bytef*>(data), length)) ^ 0xffffffffu;
}

inline const uint8_t* fixtureGlyphBitmap(const EpdFontData* data, const EpdGlyph* glyph) {
  if (!glyph->width || !glyph->height) return nullptr;
  if (!data->groups) return data->bitmap + glyph->dataOffset;
  static FontDecompressor decompressor;
  static const bool ready = decompressor.init();
  if (!ready) std::abort();
  const auto* bitmap = decompressor.getBitmap(data, glyph, static_cast<uint32_t>(glyph - data->glyph));
  if (!bitmap) std::abort();
  return bitmap;
}
