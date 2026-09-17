#pragma once

#include <cstdint>
#include <memory>
#include <vector>

class GfxRenderer;
class TextBlock;

namespace textsettings {

inline constexpr size_t FIRST_PARAGRAPH_LINES = 2;
inline constexpr size_t SECOND_PARAGRAPH_LINES = 1;

// Settings + geometry that determine the laid-out lines; used to invalidate the cache.
struct PreviewKey {
  int fontId = -1;
  uint32_t fontFamilyIdentity = 0;
  int fontPointSize = -1;
  int screenMargin = -1;
  int textWidth = -1;
  float lineCompression = -1.0f;
  uint8_t alignment = 0xFF;
  uint8_t extraParagraphSpacing = 0;
  uint8_t paragraphIndent = 0;
  uint8_t letterSpacing = 1;
  uint8_t wordSpacing = 0;
  uint8_t dropCapMode = 0;
  uint8_t inkWeight = 0xFF;
  uint8_t language = 0xFF;
  bool hyphenation = false;
  bool operator==(const PreviewKey&) const = default;
};

// Cached engine preview lines + the key that produced them
struct PreviewLayout {
  PreviewLayout();
  ~PreviewLayout();

  std::vector<std::unique_ptr<TextBlock>> lines;
  // So dong cua doan thu nhat. Pan ghep hai doan qua cung mot danh sach dong, nen cho ve can
  // biet ranh gioi de chen khoang cach doan - va de dong dau doan hai lo ro net thut dau dong
  // (doan mot da bi chu lon dau chuong chiem cho nen khong the hien muc thut).
  size_t firstParagraphLines = 0;
  PreviewKey key;
};

// Draws the sample-text pane via the reader engine, reusing layout across redraws
void renderPreview(const GfxRenderer& renderer, PreviewLayout& layout, int previewPadding, int labelGap, int top,
                   int height, const char* familyName, const char* sizeName);

}  // namespace textsettings
