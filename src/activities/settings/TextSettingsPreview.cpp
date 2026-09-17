#include "TextSettingsPreview.h"

#include <EpdFontFamily.h>
#include <Epub/ParsedText.h>
#include <Epub/ReaderSpacing.h>
#include <Epub/blocks/BlockStyle.h>
#include <Epub/blocks/TextBlock.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Utf8.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>

#include "CrossPointSettings.h"
#include "fontIds.h"

namespace textsettings {

PreviewLayout::PreviewLayout() = default;
PreviewLayout::~PreviewLayout() = default;

namespace {

// Map the paragraph-alignment setting to the engine's CssTextAlign (BOOK_STYLE = justified)
CssTextAlign toCssAlign(uint8_t align) {
  if (align == CrossPointSettings::BOOK_STYLE) return CssTextAlign::Justify;
  return static_cast<CssTextAlign>(align);
}

uint32_t previewFontFamilyIdentity() {
  uint32_t hash = 2166136261u;
  hash ^= SETTINGS.fontFamily;
  hash *= 16777619u;
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(SETTINGS.sdFontFamilyName); *p; ++p) {
    hash ^= *p;
    hash *= 16777619u;
  }
  return hash;
}

// Leave CJK line breaking and NFC composition to the same ParsedText path as the reader.
std::vector<std::string> sampleTokens(const char* text) {
  std::vector<std::string> tokens;
  std::string word;
  for (const char* p = text;; ++p) {
    if (*p == ' ' || *p == '\0') {
      if (!word.empty()) {
        tokens.push_back(std::move(word));
        word.clear();
      }
      if (*p == '\0') break;
    } else {
      word.push_back(*p);
    }
  }
  return tokens;
}

// Lay the sample text out through the reader engine into layout.lines
void relayout(PreviewLayout& layout, const GfxRenderer& renderer, int fontId, int textWidth) {
  layout.lines.clear();
  layout.firstParagraphLines = 0;

  BlockStyle style;
  style.alignment = toCssAlign(SETTINGS.paragraphAlignment);
  style.textAlignDefined = true;  // honor the user's choice; RTL auto-detected from text

  const int letterSpacing = readerSpacing::letterPixels(SETTINGS.letterSpacing);
  const uint8_t wordSpacing = SETTINGS.wordSpacing;

  const int dropCapHeight = readerSpacing::dropCapHeight(
      SETTINGS.dropCapMode, renderer.getLineHeight(fontId, SETTINGS.getReaderLineCompression()));

  const std::vector<std::string> tokens = sampleTokens(I18N.get(StrId::STR_FONT_PREVIEW_TEXT));
  if (tokens.empty()) return;

  // Two lines in paragraph one expose line spacing. The next paragraph exposes
  // paragraph spacing and first-line indentation, including when a cap is enabled.
  // The reader engine owns every line break; the pane retains only its visible lines.
  for (int paragraph = 0; paragraph < 2; paragraph++) {
    ParsedText parsed(SETTINGS.extraParagraphSpacing != 0, SETTINGS.hyphenationEnabled != 0, false, style,
                      SETTINGS.paragraphIndent, letterSpacing, wordSpacing);
    parsed.setLineCompression(SETTINGS.getReaderLineCompression());
    if (paragraph == 0 && dropCapHeight > 0) parsed.enableDropCap(dropCapHeight);
    // Repeat short localized samples so the first paragraph can fill two lines.
    for (int repeat = 0; repeat < 2; ++repeat) {
      for (const std::string& word : tokens) parsed.addWord(word, EpdFontFamily::REGULAR);
    }

    const size_t before = layout.lines.size();
    const size_t lineLimit = paragraph == 0 ? FIRST_PARAGRAPH_LINES : SECOND_PARAGRAPH_LINES;
    parsed.layoutAndExtractLines(
        renderer, fontId, static_cast<uint16_t>(textWidth),
        [&layout, before, lineLimit](std::unique_ptr<TextBlock> line, uint32_t) {
          if (layout.lines.size() - before < lineLimit) layout.lines.push_back(std::move(line));
        });
    if (paragraph == 0) layout.firstParagraphLines = layout.lines.size() - before;
  }
}

}  // namespace

void renderPreview(const GfxRenderer& renderer, PreviewLayout& layout, int previewPadding, int labelGap, int top,
                   int height, const char* familyName, const char* sizeName) {
  const int left = previewPadding;
  const int width = renderer.getScreenWidth() - (previewPadding * 2);
  if (width <= 0 || height <= 0) return;

  const int labelH = renderer.getTextHeight(UI_10_FONT_ID);
  char labelBuf[128];
  snprintf(labelBuf, sizeof(labelBuf), "%s \"%s, %s\"", tr(STR_PREVIEW), familyName, sizeName);
  const int labelY = top + height - previewPadding - labelH;
  const char* labelText = labelBuf;
  std::string clippedLabel;
  if (renderer.getTextWidth(UI_10_FONT_ID, labelBuf) > width) {
    clippedLabel = renderer.truncatedText(UI_10_FONT_ID, labelBuf, width, EpdFontFamily::REGULAR);
    labelText = clippedLabel.c_str();
  }
  renderer.drawText(UI_10_FONT_ID, left, labelY, labelText);

  const int fontId = SETTINGS.getReaderFontId();
  if (fontId == 0) return;

  const int lineH = renderer.getTextHeight(fontId);
  if (lineH <= 0) return;

  const int textLeft = left + SETTINGS.screenMargin;
  const int textWidth = width - 2 * SETTINGS.screenMargin;
  if (textWidth <= 0) return;

  const float compression = SETTINGS.getReaderLineCompression();
  const int lineAdvance = std::max(1, renderer.getLineHeight(fontId, compression));
  const int paragraphGap = readerSpacing::paragraphGap(SETTINGS.extraParagraphSpacing, lineAdvance);

  // Re-lay out only when a setting, locale, font identity or text width changes.
  const PreviewKey key{.fontId = fontId,
                       .fontFamilyIdentity = previewFontFamilyIdentity(),
                       .fontPointSize = SETTINGS.fontPointSize,
                       .screenMargin = SETTINGS.screenMargin,
                       .textWidth = textWidth,
                       .lineCompression = compression,
                       .alignment = SETTINGS.paragraphAlignment,
                       .extraParagraphSpacing = SETTINGS.extraParagraphSpacing,
                       .paragraphIndent = SETTINGS.paragraphIndent,
                       .letterSpacing = SETTINGS.letterSpacing,
                       .wordSpacing = SETTINGS.wordSpacing,
                       .dropCapMode = SETTINGS.dropCapMode,
                       .inkWeight = SETTINGS.readerInkWeight,
                       .language = static_cast<uint8_t>(I18N.getLanguage()),
                       .hyphenation = SETTINGS.hyphenationEnabled != 0};
  if (key != layout.key) {
    if (auto* fcm = renderer.getFontCacheManager()) {
      fcm->clearCache();
      const std::string sample = utf8ComposeNfc(I18N.get(StrId::STR_FONT_PREVIEW_TEXT));
      fcm->prewarmCache(fontId, sample.c_str(),
                       SETTINGS.dropCapMode != readerSpacing::DROP_CAP_OFF ? 0x03 : 0x01);
    }
    relayout(layout, renderer, fontId, textWidth);
    layout.key = key;
  }

  const int textBottomLimit = labelY - labelGap;

  // Keep the second paragraph below the actual drop-cap extent, matching the reader page builder.
  int y = top + previewPadding;
  int firstParagraphBottom = y;
  for (size_t i = 0; i < layout.lines.size(); i++) {
    if (i == layout.firstParagraphLines) y = std::max(y, firstParagraphBottom) + paragraphGap;
    const int dropCapExtent = static_cast<int>(layout.lines[i]->getDropCapHeight()) + 4;
    const int visualExtent = std::max(lineAdvance, std::max(lineH, dropCapExtent));
    if (y + visualExtent > textBottomLimit) return;
    if (i < layout.firstParagraphLines && layout.lines[i]->getDropCapHeight() > 0) {
      firstParagraphBottom = std::max(firstParagraphBottom, y + dropCapExtent);
    }
    layout.lines[i]->render(renderer, fontId, textLeft, y);
    y += lineAdvance;
  }
}

}  // namespace textsettings
