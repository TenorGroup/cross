#include "TextBlock.h"

#include <BidiUtils.h>
#include <GfxRenderer.h>
#include <HalMemory.h>
#include <HeapFloor.h>
#include <Logging.h>
#include <Memory.h>
#include <MemoryManager.h>
#include <Serialization.h>

#include <algorithm>
#include <cstring>

#include "../../../../src/fontIds.h"

size_t TextBlock::arenaSize(const uint16_t wordCount, const bool hasFocus, const uint16_t textBytes) {
  // Layout documented in TextBlock.h: 16-bit arrays first, then 8-bit arrays, then text.
  size_t size =
      static_cast<size_t>(wordCount) * (sizeof(SourceRange) + sizeof(uint16_t) + sizeof(int16_t) + sizeof(uint8_t));
  if (hasFocus) {
    size += static_cast<size_t>(wordCount) * (sizeof(uint16_t) + sizeof(uint8_t));
  }
  return size + textBytes;
}

void TextBlock::bindArenaPointers() {
  uint8_t* base = arena.get();
  const size_t wc = numWords;
  sourceRanges = reinterpret_cast<const SourceRange*>(base);
  base += wc * sizeof(SourceRange);
  textOffArr = reinterpret_cast<const uint16_t*>(base);
  xposArr = reinterpret_cast<const int16_t*>(base + wc * 2);
  size_t off = wc * 4;
  if (focusPresent) {
    focusSuffixXArr = reinterpret_cast<const uint16_t*>(base + off);
    off += wc * 2;
  }
  stylesArr = base + off;
  off += wc;
  if (focusPresent) {
    focusBoundaryArr = base + off;
    off += wc;
  }
  textArr = reinterpret_cast<const char*>(base + off);
}

TextBlock::TextBlock(const std::vector<std::string>& words, const std::vector<int16_t>& wordXpos,
                     const std::vector<EpdFontFamily::Style>& wordStyles, const std::vector<uint8_t>& focusBoundary,
                     const std::vector<uint16_t>& focusSuffixX, const BlockStyle& blockStyle,
                     std::vector<std::string> rubyTexts, std::vector<LinkSpan> linkSpans,
                     const std::vector<SourceRange>& ranges, const uint16_t paragraphStartWord)
    : blockStyle(blockStyle), paragraphStartWord(paragraphStartWord), linkSpans(std::move(linkSpans)) {
  // Focus annotations are optional: empty vectors mean no word in this block has a split.
  // When present, they must be sized in lockstep with words[].
  const bool hasFocus = !focusBoundary.empty();
  if ((!ranges.empty() && ranges.size() != words.size()) || words.size() != wordXpos.size() ||
      words.size() != wordStyles.size() || words.size() > 10000 ||
      (paragraphStartWord != UINT16_MAX && paragraphStartWord >= words.size()) ||
      (hasFocus && (words.size() != focusBoundary.size() || words.size() != focusSuffixX.size()))) {
    LOG_ERR("TXB", "Construction failed: size mismatch (words=%u, xpos=%u, styles=%u, boundary=%u, suffixX=%u)",
            static_cast<uint32_t>(words.size()), static_cast<uint32_t>(wordXpos.size()),
            static_cast<uint32_t>(wordStyles.size()), static_cast<uint32_t>(focusBoundary.size()),
            static_cast<uint32_t>(focusSuffixX.size()));
    isValid = false;
    return;
  }

  numWords = static_cast<uint16_t>(words.size());
  focusPresent = hasFocus;
  if (numWords == 0) {
    return;  // valid empty block, no arena
  }

  // Pass 1: total text size, one NUL per word. A line is at most a physical
  // row of the page, so uint16_t offsets are ample; reject anything larger.
  size_t totalText = 0;
  for (const auto& w : words) totalText += w.size() + 1;
  if (totalText > UINT16_MAX) {
    LOG_ERR("TXB", "Construction failed: text size %u exceeds arena limit", static_cast<uint32_t>(totalText));
    numWords = 0;
    focusPresent = false;
    isValid = false;
    return;
  }
  textBytes = static_cast<uint16_t>(totalText);

  const size_t size = arenaSize(numWords, focusPresent, textBytes);
  arena = heapfloor::array<uint8_t[]>(size, "text-arena");
  if (!arena) {
    // Evict rebuildable caches (SD-font mini data, render glyph cache) and
    // retry once before declaring the line lost.
    freeink::MemoryManager::instance().ensureFree(size + 4 * 1024);
    arena = heapfloor::array<uint8_t[]>(size, "text-arena");
  }
  if (!arena) {
    LOG_ERR("TXB", "OOM: arena %u bytes", static_cast<uint32_t>(size));
    numWords = 0;
    textBytes = 0;
    focusPresent = false;
    isValid = false;
    return;
  }
  bindArenaPointers();

  // Pass 2: fill. Mutable aliases of the const views bound above.
  auto* textOff = const_cast<uint16_t*>(textOffArr);
  auto* xpos = const_cast<int16_t*>(xposArr);
  auto* styles = const_cast<uint8_t*>(stylesArr);
  auto* text = const_cast<char*>(textArr);
  uint16_t off = 0;
  for (uint16_t i = 0; i < numWords; i++) {
    const_cast<SourceRange*>(sourceRanges)[i] = ranges.empty() ? SourceRange{} : ranges[i];
    textOff[i] = off;
    xpos[i] = wordXpos[i];
    styles[i] = static_cast<uint8_t>(wordStyles[i]);
    memcpy(text + off, words[i].data(), words[i].size());
    off += static_cast<uint16_t>(words[i].size());
    text[off++] = '\0';
  }
  if (focusPresent) {
    auto* suffixX = const_cast<uint16_t*>(focusSuffixXArr);
    auto* boundary = const_cast<uint8_t*>(focusBoundaryArr);
    for (uint16_t i = 0; i < numWords; i++) {
      suffixX[i] = focusSuffixX[i];
      boundary[i] = focusBoundary[i];
    }
  }
  for (uint16_t i = 0; i < numWords && i < rubyTexts.size(); ++i) {
    if (rubyTexts[i].empty()) continue;
    if (char* text = appendRuby(i, rubyTexts[i].size())) {
      memcpy(text, rubyTexts[i].data(), rubyTexts[i].size());
    }
  }
  // A line reserves ruby space only for the annotations kept in its cache.
  rubyPresent = rubyCount != 0;
}

// Ruby is rare, so a line keeps it in a sparse array of bounded, fallible allocations instead of
// a string per word. An annotation the heap or the line limit refuses is left out, logged once.
char* TextBlock::appendRuby(const uint16_t word, const size_t length) {
  rubyPresent = true;
  if (rubyRefused) return nullptr;
  const auto refuse = [&]() -> char* {
    rubyRefused = true;
    LOG_ERR("TXB", "Ruby omitted after allocation refusal or line limit");
#ifdef TENOR_PRESS_PROBE
    LOG_INF("TXB", "RUBY_ALLOC_REFUSE count=%u bytes=%u", rubyCount, static_cast<unsigned>(length));
#endif
    return nullptr;
  };
  if (rubyCount == MAX_RUBIES || length > MAX_RUBY_ANNOTATION_BYTES || length >= MAX_RUBY_BYTES - rubyBytes) {
    return refuse();
  }
  if (rubyCount == rubyCapacity) {
    const size_t capacity = std::min(MAX_RUBIES, std::max(size_t(1), size_t(rubyCapacity) * 2));
    auto grown = heapfloor::array<RubyText[]>(capacity, "ruby-entries");
    if (!grown) return refuse();
    for (uint16_t i = 0; i < rubyCount; ++i) grown[i] = std::move(rubyTexts[i]);
    rubyTexts = std::move(grown);
    rubyCapacity = capacity;
  }
  auto text = heapfloor::array<char[]>(length + 1, "ruby-text");
  if (!text) return refuse();
  char* data = text.get();
  rubyTexts[rubyCount++] = {word, static_cast<uint16_t>(length), std::move(text)};
  rubyBytes += length + 1;
  return data;
}

bool TextBlock::hasRuby() const { return rubyPresent; }

std::string_view TextBlock::getRubyText(const uint16_t word) const {
  size_t first = 0, end = rubyCount;
  while (first < end) {
    const size_t middle = first + (end - first) / 2;
    if (rubyTexts[middle].word < word) first = middle + 1;
    else end = middle;
  }
  if (first == rubyCount || rubyTexts[first].word != word) return {};
  return {rubyTexts[first].text.get(), rubyTexts[first].length};
}

// Whether the line's ink could touch the grayscale strip being drawn. The box comes from the black
// and white pass; without one (first draw, other renderer or font) the answer is yes.
bool TextBlock::intersectsStrip(const GfxRenderer& renderer, const int fontId, const int x, const int y) const {
  if (renderer.isFontCacheScanning() || renderer.getRenderMode() == GfxRenderer::BW ||
      boundsRenderer != &renderer || boundsFont != fontId) {
    return true;
  }
  return renderer.glyphIntersectsStrip(x + stripBounds[0], y + stripBounds[1], x + stripBounds[2],
                                       y + stripBounds[3]);
}

bool TextBlock::setWordFonts(const uint8_t* slots, const int32_t (&fonts)[MAX_WORD_FONTS]) {
  wordFontData = makeUniqueNoThrow<uint8_t[]>(sizeof(fonts) + numWords);
  if (!wordFontData) return false;
  memcpy(wordFontData.get(), fonts, sizeof(fonts));
  memcpy(wordFontData.get() + sizeof(fonts), slots, numWords);
  return true;
}

int TextBlock::renderFontId(const GfxRenderer& renderer, const int fontId, const int sectionFontId) {
  return fontId == sectionFontId || renderer.ensureFontLoaded(fontId) ? fontId : sectionFontId;
}

int TextBlock::wordFontId(const uint16_t i, const int sectionFontId) const {
  if (!wordFontData) return blockFontId(sectionFontId);
  const uint8_t slot = wordFontData[sizeof(int32_t) * MAX_WORD_FONTS + i] & ~SMALL_CAPS_SLOT_FLAG;
  if (slot == 0) return blockFontId(sectionFontId);
  int32_t font = 0;
  memcpy(&font, wordFontData.get() + sizeof(int32_t) * (slot - 1), sizeof(font));
  return font;
}

int TextBlock::lineAscent(const GfxRenderer& renderer, const int sectionFontId) const {
  int ascent = renderer.getFontAscenderSize(renderFontId(renderer, blockFontId(sectionFontId), sectionFontId));
  if (!wordFontData) return ascent;
  for (uint8_t slot = 0; slot < MAX_WORD_FONTS; ++slot) {
    int32_t font = 0;
    memcpy(&font, wordFontData.get() + sizeof(int32_t) * slot, sizeof(font));
    if (font != 0) ascent = std::max(ascent, renderer.getFontAscenderSize(renderFontId(renderer, font, sectionFontId)));
  }
  return ascent;
}

int TextBlock::wordYOffset(const GfxRenderer& renderer, const int sectionFontId, const uint16_t i) const {
  if (!wordFontData) return 0;
  return lineAscent(renderer, sectionFontId) -
         renderer.getFontAscenderSize(renderFontId(renderer, wordFontId(i, sectionFontId), sectionFontId));
}

int TextBlock::extraAscent(const GfxRenderer& renderer, const int sectionFontId) const {
  if (!wordFontData) return 0;
  return lineAscent(renderer, sectionFontId) -
         renderer.getFontAscenderSize(renderFontId(renderer, blockFontId(sectionFontId), sectionFontId));
}

void TextBlock::render(const GfxRenderer& renderer, const int sectionFontId, const int x, const int y) const {
  if (!isValid) {
    LOG_ERR("TXB", "Render skipped: invalid block");
    return;
  }
  // A sized variant that can no longer be loaded falls back to the section font.
  const int fontId = renderFontId(renderer, blockFontId(sectionFontId), sectionFontId);
  const int8_t tracking = blockStyle.characterSpacing;

  const bool scanning = renderer.isFontCacheScanning();
  // Words in other sizes share the baseline set by the line's tallest font.
  const int ascender = lineAscent(renderer, sectionFontId);

  // The first black and white draw records the ink box, so the grayscale passes can skip a line
  // outside the strip. Mixed sizes and small caps are not measured by the box: no box for them.
  const bool capture = !scanning && renderer.getRenderMode() == GfxRenderer::BW && !renderer.isStripTargetActive() &&
                       !wordFontData;
  bool boundsValid = capture;
  std::array<int, 4> bounds = {32767, 32767, -32768, -32768};
  if (capture || scanning) boundsRenderer = nullptr;
  const auto drawText = [&](int font, int dx, int dy, const char* text, EpdFontFamily::Style style,
                            BidiUtils::BidiBaseDir dir) {
    renderer.drawText(font, dx, dy, text, true, style, dir, tracking);
    if (boundsValid) {
      boundsValid = (style & EpdFontFamily::SMALL_CAPS) == 0 &&
                    renderer.extendTextBounds(bounds, font, dx - x, dy - y, text, style, dir, tracking);
    }
  };

  struct DecorationLineTracker {
    EpdFontFamily::Style style;
    int startX = -1;
    int endX = -1;
    int yPos = 0;

    bool active() const { return startX != -1; }
    void reset() {
      startX = -1;
      endX = -1;
      yPos = 0;
    }
  };

  DecorationLineTracker decorationLines[] = {
      {EpdFontFamily::UNDERLINE},
      {EpdFontFamily::STRIKETHROUGH},
  };

  const auto flushDecoration = [&](DecorationLineTracker& line) {
    if (line.active()) {
      renderer.drawLine(line.startX, line.yPos, line.endX, line.yPos, 2, true);
      if (boundsValid) {
        bounds[0] = std::min(bounds[0], std::min(line.startX, line.endX) - x);
        bounds[1] = std::min(bounds[1], line.yPos - y);
        bounds[2] = std::max(bounds[2], std::max(line.startX, line.endX) - x);
        bounds[3] = std::max(bounds[3], line.yPos + 1 - y);
      }
      line.reset();
    }
  };
  const auto flushDecorations = [&]() {
    for (auto& line : decorationLines) {
      flushDecoration(line);
    }
  };

  // Loop-invariant: hoisted out of the word loop.
  const int rubyShift = getRubyShift(ascender);

  for (uint16_t i = 0; i < numWords; i++) {
    const char* word = wordText(i);
    const int wordX = xposArr[i] + x;
    const EpdFontFamily::Style currentStyle = wordStyle(i);
    const auto baseDir =
        static_cast<BidiUtils::BidiBaseDir>(BidiUtils::detectParagraphLevel(word, blockStyle.isRtl ? 1 : 0));
    const uint8_t boundary = focusBoundary(i);
    const int wordFont = wordFontData ? renderFontId(renderer, wordFontId(i, sectionFontId), sectionFontId) : fontId;
    const int wordAscender = wordFont == fontId && !wordFontData ? ascender : renderer.getFontAscenderSize(wordFont);

    // SUP/SUB shift the baseline passed to drawText; the glyph is also scaled 50% inside
    // drawText, so these offsets are chosen relative to the full-size ascender:
    //   SUP: raise by 40% of ascender - sits clearly above the cap-height
    //   SUB: lower by 25% of ascender - descends below baseline without clashing with ascenders below
    int wordY = y + rubyShift + (ascender - wordAscender);
    if ((currentStyle & EpdFontFamily::SUP) != 0) {
      wordY -= ascender * 2 / 5;
    } else if ((currentStyle & EpdFontFamily::SUB) != 0) {
      wordY += ascender / 4;
    }

    const int drawX = wordX;

    if (i == 0 && dropCapHeight) {
      // The reader's large initial: the first word of the paragraph's first line.
      renderer.drawDropCapWord(wordFont, drawX, wordY, word, currentStyle, dropCapHeight, tracking);
      if (boundsValid) {
        boundsValid = renderer.extendTextBounds(bounds, wordFont, drawX - x, wordY - y, word, currentStyle,
                                                BidiUtils::BidiBaseDir::AUTO, tracking, dropCapHeight);
      }
    } else if (boundary > 0) {
      // Focus split: draw bold prefix, then the regular suffix at a pre-computed x offset.
      // The bold prefix is bounded to 9 codepoints by the clamp on targetBoldChars in
      // ParsedText::addWord; 9 UTF-8 codepoints occupy at most 9 * 4 = 36 bytes, +1 for null = 37.
      // suffixX is computed at cache-creation time to avoid font metric lookups at render time.
      static constexpr size_t MAX_FOCUS_PREFIX_BYTES = 9 * 4 + 1;
      char boldBuf[40];
      static_assert(sizeof(boldBuf) >= MAX_FOCUS_PREFIX_BYTES,
                    "boldBuf too small for max focus prefix (9 codepoints * 4 UTF-8 bytes + null)");
      const auto boldStyle = static_cast<EpdFontFamily::Style>(currentStyle | EpdFontFamily::BOLD);
      const size_t boldLen =
          std::min<size_t>({static_cast<size_t>(boundary), static_cast<size_t>(wordTextLen(i)), sizeof(boldBuf) - 1});
      memcpy(boldBuf, word, boldLen);
      boldBuf[boldLen] = '\0';
      drawText(wordFont, drawX, wordY, boldBuf, boldStyle, baseDir);
      const int suffixX = drawX + focusSuffixXArr[i];
      drawText(wordFont, suffixX, wordY, word + boldLen, currentStyle, baseDir);
    } else {
      drawText(wordFont, drawX, wordY, word, currentStyle, baseDir);
    }

    // Horizontal ruby text, centred over its group. Layout (extractLine) has already reserved
    // extraStartOffset on the left and extraEndOffset on the right, so the centred x stays within
    // the page margins.
    const auto ruby = getRubyText(i);
    if (!ruby.empty() && (currentStyle & EpdFontFamily::RUBY_CONTINUE) == 0) {
      int groupWordCount = 1;
      while (i + groupWordCount < numWords && (wordStyle(i + groupWordCount) & EpdFontFamily::RUBY_CONTINUE) != 0) {
        ++groupWordCount;
      }
      int groupWidth = 0;
      for (int k = 0; k < groupWordCount; ++k) {
        groupWidth += renderer.getTextAdvanceX(fontId, wordText(i + k), wordStyle(i + k), tracking);
      }
      const int rubyWidth = renderer.getTextAdvanceX(fontId, ruby.data(), EpdFontFamily::SUP, tracking);
      drawText(fontId, wordX - (rubyWidth - groupWidth) / 2, wordY - ascender, ruby.data(), EpdFontFamily::SUP,
               baseDir);
    }

    if (scanning) {
      continue;
    }

    if (EpdFontFamily::hasTextDecoration(currentStyle)) {
      int lineStartX = drawX;
      int lineWidth = i == 0 && dropCapHeight
                          ? renderer.getDropCapWordWidth(wordFont, word, currentStyle, dropCapHeight, tracking)
                          : renderer.getTextAdvanceX(wordFont, word, currentStyle, tracking, baseDir,
                                                     GfxRenderer::TextMeasureMode::Rendered);

      // Do not decorate the synthetic em-space used for paragraph indentation.
      if (wordTextLen(i) >= 3 && static_cast<uint8_t>(word[0]) == 0xE2 && static_cast<uint8_t>(word[1]) == 0x80 &&
          static_cast<uint8_t>(word[2]) == 0x83) {
        const char* visibleText = word + 3;
        lineStartX += renderer.getTextAdvanceX(wordFont, "\xe2\x80\x83", currentStyle, tracking, baseDir,
                                               GfxRenderer::TextMeasureMode::Rendered);
        lineWidth = renderer.getTextAdvanceX(wordFont, visibleText, currentStyle, tracking, baseDir,
                                             GfxRenderer::TextMeasureMode::Rendered);
      }

      for (auto& line : decorationLines) {
        if ((currentStyle & line.style) == 0) {
          flushDecoration(line);
          continue;
        }

        int offset = 2;
        if (line.style == EpdFontFamily::STRIKETHROUGH) {
          int32_t advance = 0;
          int height = 0;
          const bool smallCaps = (currentStyle & EpdFontFamily::SMALL_CAPS) != 0;
          if (!renderer.getCodepointMetrics(wordFont, smallCaps ? 'X' : 'x', currentStyle, advance, height) ||
              height <= 0) {
            height = wordAscender * 2 / 3;
          }
          if ((currentStyle & (EpdFontFamily::SUP | EpdFontFamily::SUB)) != 0) {
            height = (height + 1) / 2;
          } else if (smallCaps) {
            height = (height * 3 + 3) / 4;
          }
          offset = -std::max(1, height / 2);
        }
        const int lineY = wordY + wordAscender + offset;
        if (line.active() && line.yPos != lineY) {
          flushDecoration(line);
        }
        if (!line.active()) {
          line.startX = lineStartX;
          line.yPos = lineY;
        }
        line.endX = lineStartX + lineWidth;
      }
    } else {
      flushDecorations();
    }
  }
  flushDecorations();
  if (boundsValid && bounds[0] <= bounds[2] && bounds[1] <= bounds[3] && bounds[0] > -32768 && bounds[1] > -32768 &&
      bounds[2] < 32767 && bounds[3] < 32767) {
    stripBounds = {static_cast<int16_t>(bounds[0] - 1), static_cast<int16_t>(bounds[1] - 1),
                   static_cast<int16_t>(bounds[2] + 1), static_cast<int16_t>(bounds[3] + 1)};
    boundsFont = fontId;
    boundsRenderer = &renderer;
  }
}

bool TextBlock::serialize(HalFile& file) const {
  if (!isValid) {
    LOG_ERR("TXB", "Serialization failed: invalid block");
    return false;
  }

  // The scalars around the arena go out in one write each instead of one per field: a chapter
  // build wrote a page as some 700 small writes, each through the storage lock (X3 r40, 15 ms a
  // page). The bytes are the fields back to back, exactly what writePod wrote one at a time.
  uint8_t fields[64];
  size_t used = 0;
  const auto put = [&fields, &used](const auto& value) {
    static_assert(std::is_trivially_copyable<std::remove_reference_t<decltype(value)>>::value);
    memcpy(fields + used, &value, sizeof(value));
    used += sizeof(value);
  };
  const auto flush = [&file, &fields, &used] {
    const bool ok = file.write(fields, used) == used;
    used = 0;
    return ok;
  };

  // Word data: scalars, then the arena verbatim -- its in-memory layout is
  // exactly the on-disk layout (see TextBlock.h), so one write covers all
  // per-word arrays and the text blob.
  put(numWords);
  put(static_cast<uint8_t>(focusPresent ? 1 : 0));
  put(textBytes);
  put(paragraphStartWord);
  put(dropCapHeight);
  if (!flush()) return false;
  if (numWords > 0) {
    const size_t size = arenaSize(numWords, focusPresent, textBytes);
    if (file.write(arena.get(), size) != size) {
      LOG_ERR("TXB", "Serialization failed: arena write (%u bytes)", static_cast<uint32_t>(size));
      return false;
    }
  }

  // Ruby text data: a length and the text per word. A line without ruby is a zero length per word.
  if (rubyCount == 0) {
    static constexpr uint8_t kZeroLengths[64] = {};
    for (size_t left = static_cast<size_t>(numWords) * sizeof(uint32_t); left > 0;) {
      const size_t chunk = std::min(left, sizeof(kZeroLengths));
      if (file.write(kZeroLengths, chunk) != chunk) return false;
      left -= chunk;
    }
  } else {
    for (size_t i = 0; i < numWords; i++) {
      const auto ruby = getRubyText(i);
      if (!serialization::writePod(file, static_cast<uint32_t>(ruby.size())) ||
          (!ruby.empty() && file.write(reinterpret_cast<const uint8_t*>(ruby.data()), ruby.size()) != ruby.size())) {
        return false;
      }
    }
  }

  // Style (alignment + margins/padding/indent + spacing and font) and the per-word font flag.
  put(blockStyle.alignment);
  put(blockStyle.textAlignDefined);
  put(blockStyle.marginTop);
  put(blockStyle.marginBottom);
  put(blockStyle.marginLeft);
  put(blockStyle.marginRight);
  put(blockStyle.paddingTop);
  put(blockStyle.paddingBottom);
  put(blockStyle.paddingLeft);
  put(blockStyle.paddingRight);
  put(blockStyle.textIndent);
  put(blockStyle.textIndentDefined);
  put(blockStyle.isRtl);
  put(blockStyle.directionDefined);
  put(blockStyle.characterSpacing);
  put(blockStyle.fontId);
  // Per-word fonts: a flag, then the font table and one slot per word.
  const uint8_t hasWordFonts = wordFontData ? 1 : 0;
  put(hasWordFonts);
  if (!flush()) return false;
  if (hasWordFonts) {
    const size_t size = sizeof(int32_t) * MAX_WORD_FONTS + numWords;
    if (file.write(wordFontData.get(), size) != size) {
      LOG_ERR("TXB", "Serialization failed: word fonts");
      return false;
    }
  }

  return true;
}

std::unique_ptr<TextBlock> TextBlock::deserialize(HalFile& file) {
  serialization::CheckedReader reader(file);
  uint16_t wc;
  uint8_t hasFocus;
  uint16_t textBytes;
  uint16_t paragraphStartWord;
  uint16_t dropHeight = 0;
  reader.pod(wc);
  reader.pod(hasFocus);
  reader.pod(textBytes);
  reader.pod(paragraphStartWord);
  reader.pod(dropHeight);
  if (!reader.ok() || hasFocus > 1 || dropHeight > 256) return nullptr;

  // Sanity checks: cap the arena allocation and reject impossible geometry
  // (every word carries at least its NUL terminator).
  if (wc > 10000 || (paragraphStartWord != UINT16_MAX && paragraphStartWord >= wc)) {
    LOG_ERR("TXB", "Deserialization failed: word count %u exceeds maximum", wc);
    return nullptr;
  }
  if ((wc == 0 && textBytes != 0) || (wc > 0 && textBytes < wc)) {
    LOG_ERR("TXB", "Deserialization failed: bad text size %u for %u words", textBytes, wc);
    return nullptr;
  }

  std::unique_ptr<TextBlock> block(new (std::nothrow) TextBlock());
  if (!block) {
    LOG_ERR("TXB", "OOM: TextBlock");
    return nullptr;
  }
  block->dropCapHeight = dropHeight;
  block->numWords = wc;
  block->textBytes = textBytes;
  block->paragraphStartWord = paragraphStartWord;
  block->focusPresent = hasFocus != 0;

  if (wc > 0) {
    const size_t size = arenaSize(wc, block->focusPresent, textBytes);
    // Each word also has a ruby length field; the style is the bytes counted below. Reject
    // truncated records before the arena allocation.
    constexpr size_t styleBytes = sizeof(CssTextAlign) + 4 * sizeof(bool) + 9 * sizeof(int16_t) +
                                  sizeof(BlockStyle::characterSpacing) + sizeof(BlockStyle::fontId) + 1;
    if (!reader.has(size + static_cast<size_t>(wc) * sizeof(uint32_t) + styleBytes)) return nullptr;
    block->arena = heapfloor::array<uint8_t[]>(size, "text-arena");
    if (!block->arena) {
      LOG_ERR("TXB", "OOM: arena %u bytes", static_cast<uint32_t>(size));
      return nullptr;
    }
    if (!reader.read(block->arena.get(), size)) {
      LOG_ERR("TXB", "Deserialization failed: arena read (%u bytes)", static_cast<uint32_t>(size));
      return nullptr;
    }
    block->bindArenaPointers();

    // Validate offsets before anything dereferences wordText(): offset 0 first,
    // strictly increasing, in bounds, and every word NUL-terminated (word i ends
    // at the byte before offset i+1; the last word at the last text byte).
    const uint16_t* textOff = block->textOffArr;
    const char* text = block->textArr;
    if (textOff[0] != 0 || text[textBytes - 1] != '\0') {
      LOG_ERR("TXB", "Deserialization failed: corrupt text layout");
      return nullptr;
    }
    for (uint16_t i = 1; i < wc; i++) {
      if (textOff[i] <= textOff[i - 1] || textOff[i] >= textBytes || text[textOff[i] - 1] != '\0') {
        LOG_ERR("TXB", "Deserialization failed: corrupt word offset %u", i);
        return nullptr;
      }
    }
  }

  // Ruby text data: a length per word, the text when the length is not zero. Ruby is a CJK feature,
  // so for nearly every book every length is zero and nothing is allocated; an annotation the
  // heap refuses is skipped (its bytes are read and dropped).
  for (uint16_t i = 0; i < wc; ++i) {
    uint32_t length = 0;
    if (!reader.pod(length) || length > MAX_RUBY_ANNOTATION_BYTES || !reader.has(length)) return nullptr;
    if (length == 0) continue;
    if (char* text = block->appendRuby(i, length)) {
      if (!reader.read(text, length)) return nullptr;
    } else {
      char discarded[64];
      while (length) {
        const size_t bytes = std::min(size_t(length), sizeof(discarded));
        if (!reader.read(discarded, bytes)) return nullptr;
        length -= bytes;
      }
    }
  }

  // Style (alignment + margins/padding/indent + spacing and font)
  BlockStyle& blockStyle = block->blockStyle;
  reader.pod(blockStyle.alignment);
  reader.pod(blockStyle.textAlignDefined);
  reader.pod(blockStyle.marginTop);
  reader.pod(blockStyle.marginBottom);
  reader.pod(blockStyle.marginLeft);
  reader.pod(blockStyle.marginRight);
  reader.pod(blockStyle.paddingTop);
  reader.pod(blockStyle.paddingBottom);
  reader.pod(blockStyle.paddingLeft);
  reader.pod(blockStyle.paddingRight);
  reader.pod(blockStyle.textIndent);
  reader.pod(blockStyle.textIndentDefined);
  reader.pod(blockStyle.isRtl);
  reader.pod(blockStyle.directionDefined);
  reader.pod(blockStyle.characterSpacing);
  reader.pod(blockStyle.fontId);

  uint8_t hasWordFonts = 0;
  reader.pod(hasWordFonts);
  if (!reader.ok() || static_cast<uint8_t>(blockStyle.alignment) > static_cast<uint8_t>(CssTextAlign::None) ||
      blockStyle.characterSpacing < -2 || blockStyle.characterSpacing > 2) {
    return nullptr;
  }
  if (hasWordFonts > 1) {
    LOG_ERR("TXB", "Deserialization failed: bad word font flag %u", hasWordFonts);
    return nullptr;
  }
  if (hasWordFonts) {
    const size_t size = sizeof(int32_t) * MAX_WORD_FONTS + wc;
    block->wordFontData = makeUniqueNoThrow<uint8_t[]>(size);
    if (!block->wordFontData || !reader.read(block->wordFontData.get(), size)) {
      LOG_ERR("TXB", "Deserialization failed: word fonts");
      return nullptr;
    }
    const uint8_t* slots = block->wordFontData.get() + sizeof(int32_t) * MAX_WORD_FONTS;
    for (uint16_t i = 0; i < wc; ++i) {
      const uint8_t slot = slots[i] & ~SMALL_CAPS_SLOT_FLAG;
      int32_t font = 1;
      if (slot > 0 && slot <= MAX_WORD_FONTS) {
        memcpy(&font, block->wordFontData.get() + sizeof(int32_t) * (slot - 1), sizeof(font));
      }
      if (slot > MAX_WORD_FONTS || font == 0) {
        LOG_ERR("TXB", "Deserialization failed: bad word font slot %u", slots[i]);
        return nullptr;
      }
    }
  }

  return block;
}
