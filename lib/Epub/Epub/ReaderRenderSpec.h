#pragma once
#include <Epub/ReaderSpacing.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

// Keep the reader return-position key and section-cache format on the same
// version. A cache format change must invalidate saved page labels too.
inline constexpr uint8_t SECTION_FILE_VERSION = 58;

// The resolved text-rendering configuration a reader hands to the layout
// engine. Section-cache validation keys on every field: a section file built
// with a different spec is discarded and rebuilt.
//
// Build one via CrossPointSettings::readerRenderSpec(width, height), which
// fills every field: the settings-derived ones from the store, the viewport
// from the caller. Taking the viewport as arguments is what keeps a spec from
// existing in a half-filled state - the 0 defaults below are a last-resort
// backstop (a 0x0 viewport lays out nothing), not an invitation to omit it.
struct ReaderRenderSpec {
  int fontId = 0;
  float lineCompression = 1.0f;
  uint8_t extraParagraphSpacing = 0;
  uint8_t paragraphIndent = 0;
  int8_t letterSpacing = 0;
  // readerSpacing::Level on the U+0020 advance. Not resolved to pixels here,
  // unlike letterSpacing: the delta depends on the font's own space advance, so
  // the renderer resolves it per style at the point of use.
  uint8_t wordSpacing = 0;
  uint8_t paragraphAlignment = 0;
  uint8_t layoutKeyPadding = 0;
  uint16_t viewportWidth = 0;
  uint16_t viewportHeight = 0;
  bool hyphenationEnabled = false;
  bool embeddedStyle = true;
  uint8_t imageRendering = 0;
  // Drop cap mode, not a boolean: the resolved height comes from
  // readerSpacing::dropCapHeight(mode, lineHeight) so the reader and the
  // settings preview can never disagree.
  uint8_t dropCapMode = readerSpacing::DROP_CAP_DEFAULT;
};

static_assert(std::is_standard_layout_v<ReaderRenderSpec>, "ReaderRenderSpec must stay standard layout");
static_assert(sizeof(ReaderRenderSpec) == 24, "ReaderRenderSpec size changed");
static_assert(offsetof(ReaderRenderSpec, fontId) == 0, "ReaderRenderSpec font offset changed");
static_assert(offsetof(ReaderRenderSpec, lineCompression) == 4, "ReaderRenderSpec line offset changed");
static_assert(offsetof(ReaderRenderSpec, viewportWidth) == 14, "ReaderRenderSpec width offset changed");
static_assert(offsetof(ReaderRenderSpec, viewportHeight) == 16, "ReaderRenderSpec height offset changed");
static_assert(offsetof(ReaderRenderSpec, dropCapMode) == 21, "ReaderRenderSpec drop cap offset changed");
