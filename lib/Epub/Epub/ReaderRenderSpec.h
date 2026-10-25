#pragma once
#include <Epub/ReaderSpacing.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

// Keep the reader return-position key and section-cache format on the same
// version. A cache format change must invalidate saved page labels too.
// This is the only definition: Section.cpp uses it and does not declare its own.
inline constexpr uint8_t SECTION_FILE_VERSION = 69;

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
  // The spacing fields keep the CrossPoint names and units; CrossPointSettings::readerRenderSpec()
  // is the one place that turns the stored five-step levels into them.
  // extraParagraphSpacing is a readerSpacing::Level, not a flag: the gap after a
  // paragraph is readerSpacing::paragraphGap(level, lineHeight), and level 0 still
  // leaves the legacy gap.
  uint8_t extraParagraphSpacing = 0;
  // Spaces in the first-line indent: readerSpacing::indentSpaces(level).
  uint8_t paragraphIndentSpaces = 2;
  // Signed pixels added to every cursor advance: readerSpacing::letterPixels(level).
  int8_t characterSpacing = 0;
  // Percent of the U+0020 advance: readerSpacing::wordPermille(level) / 10.
  uint8_t wordSpacingPercent = 100;
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
  // Always false here: Focus Reading is not offered. The field stays because the
  // CrossPoint layout code reads it.
  bool focusReadingEnabled = false;
};

static_assert(std::is_standard_layout_v<ReaderRenderSpec>, "ReaderRenderSpec must stay standard layout");
static_assert(sizeof(ReaderRenderSpec) == 24, "ReaderRenderSpec size changed");
static_assert(offsetof(ReaderRenderSpec, fontId) == 0, "ReaderRenderSpec font offset changed");
static_assert(offsetof(ReaderRenderSpec, lineCompression) == 4, "ReaderRenderSpec line offset changed");
static_assert(offsetof(ReaderRenderSpec, viewportWidth) == 14, "ReaderRenderSpec width offset changed");
static_assert(offsetof(ReaderRenderSpec, viewportHeight) == 16, "ReaderRenderSpec height offset changed");
static_assert(offsetof(ReaderRenderSpec, dropCapMode) == 21, "ReaderRenderSpec drop cap offset changed");
static_assert(offsetof(ReaderRenderSpec, focusReadingEnabled) == 22, "ReaderRenderSpec focus offset changed");

// Stable signature for clipping anchors. Page-local word ordinals are valid
// only for the exact layout that produced them.
inline uint32_t readerRenderSpecSignature(const ReaderRenderSpec& spec) {
  uint32_t signature = 2166136261U;
  const auto mix = [&signature](const uint32_t value) {
    signature ^= value;
    signature *= 16777619U;
  };
  mix(static_cast<uint32_t>(spec.fontId));
  mix(static_cast<uint32_t>(spec.lineCompression * 1000.0f));
  mix(spec.extraParagraphSpacing);
  mix(spec.paragraphIndentSpaces);
  mix(static_cast<uint8_t>(spec.characterSpacing));
  mix(spec.wordSpacingPercent);
  mix(spec.paragraphAlignment);
  mix(spec.viewportWidth);
  mix(spec.viewportHeight);
  mix(spec.hyphenationEnabled);
  mix(spec.embeddedStyle);
  mix(spec.imageRendering);
  mix(spec.layoutKeyPadding);
  mix(spec.dropCapMode);
  mix(spec.focusReadingEnabled);
  return signature == 0 ? 1 : signature;
}
