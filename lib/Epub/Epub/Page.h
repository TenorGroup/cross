#pragma once
#include <HalStorage.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "FootnoteEntry.h"
#include "PageLink.h"
#include "blocks/ImageBlock.h"
#include "blocks/TextBlock.h"

enum PageElementTag : uint8_t {
  TAG_PageLine = 1,
  TAG_PageImage = 2,
  TAG_PageHorizontalRule = 3,
};

// represents something that has been added to a page
class PageElement {
 public:
  int16_t xPos;
  int16_t yPos;
  explicit PageElement(const int16_t xPos, const int16_t yPos) : xPos(xPos), yPos(yPos) {}
  virtual ~PageElement() = default;
  virtual void render(GfxRenderer& renderer, int fontId, int xOffset, int yOffset) = 0;
  virtual bool serialize(HalFile& file) = 0;
  virtual PageElementTag getTag() const = 0;  // Add type identification
};

// a line from a block element
class PageLine final : public PageElement {
  std::unique_ptr<TextBlock> block;

 public:
  PageLine(std::unique_ptr<TextBlock> block, const int16_t xPos, const int16_t yPos)
      : PageElement(xPos, yPos), block(std::move(block)) {}
  const TextBlock* getBlock() const { return block.get(); }
  void render(GfxRenderer& renderer, int fontId, int xOffset, int yOffset) override;
  bool serialize(HalFile& file) override;
  PageElementTag getTag() const override { return TAG_PageLine; }
  static std::unique_ptr<PageLine> deserialize(HalFile& file);
};

// New PageImage class
class PageImage final : public PageElement {
  std::unique_ptr<ImageBlock> imageBlock;

 public:
  PageImage(std::unique_ptr<ImageBlock> block, const int16_t xPos, const int16_t yPos)
      : PageElement(xPos, yPos), imageBlock(std::move(block)) {}
  void render(GfxRenderer& renderer, int fontId, int xOffset, int yOffset) override;
  void renderPlaceholder(GfxRenderer& renderer, int xOffset, int yOffset) const;
  bool serialize(HalFile& file) override;
  PageElementTag getTag() const override { return TAG_PageImage; }
  static std::unique_ptr<PageImage> deserialize(HalFile& file);
  const ImageBlock& getImageBlock() const { return *imageBlock; }
};

class PageHorizontalRule final : public PageElement {
  uint16_t width;
  uint8_t thickness;

 public:
  PageHorizontalRule(uint16_t width, uint8_t thickness, const int16_t xPos, const int16_t yPos)
      : PageElement(xPos, yPos), width(width), thickness(thickness) {}

  void render(GfxRenderer& renderer, int fontId, int xOffset, int yOffset) override;
  bool serialize(HalFile& file) override;
  PageElementTag getTag() const override { return TAG_PageHorizontalRule; }
  static std::unique_ptr<PageHorizontalRule> deserialize(HalFile& file);
};

// Optional page metadata has a bounded, fallible allocation path.
template <typename T, size_t Limit>
class PageMetadata {
  std::unique_ptr<T[]> entries;
  size_t count = 0;
  size_t capacity = 0;

 public:
  PageMetadata() = default;
  PageMetadata(PageMetadata&& other) noexcept { *this = std::move(other); }
  PageMetadata& operator=(PageMetadata&& other) noexcept {
    entries = std::move(other.entries);
    count = std::exchange(other.count, 0);
    capacity = std::exchange(other.capacity, 0);
    return *this;
  }
  bool resize(size_t wanted);
  bool append(const T& value) {
    const size_t index = count;
    if (!resize(index + 1)) return false;
    entries[index] = value;
    return true;
  }
  size_t size() const { return count; }
  bool empty() const { return count == 0; }
  void clear() { entries.reset(); count = capacity = 0; }
  T& operator[](size_t index) { return entries[index]; }
  const T& operator[](size_t index) const { return entries[index]; }
  T* begin() { return entries.get(); }
  T* end() { return count ? entries.get() + count : entries.get(); }
  const T* begin() const { return entries.get(); }
  const T* end() const { return count ? entries.get() + count : entries.get(); }
};

class Page {
 public:
  // the list of block index and line numbers on this page
  std::vector<std::unique_ptr<PageElement>> elements;
  static constexpr uint16_t MAX_FOOTNOTES_PER_PAGE = 16;
  static constexpr uint16_t MAX_LINKS_PER_PAGE = 32;
  using Footnotes = PageMetadata<FootnoteEntry, MAX_FOOTNOTES_PER_PAGE>;
  using Links = PageMetadata<PageLink, MAX_LINKS_PER_PAGE>;
  Footnotes footnotes;
  Links links;
  enum class LoadFailure : uint8_t { Unknown, Allocation, Corrupt };
  // Page reads, including this status, run under RenderLock.
  static inline LoadFailure loadFailure = LoadFailure::Unknown;
  static void beginLoad() { loadFailure = LoadFailure::Unknown; }

  // Zero-based visible-codepoint offset where this page starts. Not part of the serialized page
  // body (it lives in the section's visible-offset LUT); Section::loadPage* fills it in from the
  // build LUT or the on-disk LUT while the page file is already open, so the reader can persist
  // progress without a second section-file open per page turn.
  uint32_t visibleTextOffset = 0;

  void addFootnote(const char* number, const char* href) {
    if (footnotes.size() >= MAX_FOOTNOTES_PER_PAGE) return;  // Cap per-page footnotes
    FootnoteEntry entry;
    strncpy(entry.number, number, sizeof(entry.number) - 1);
    entry.number[sizeof(entry.number) - 1] = '\0';
    strncpy(entry.href, href, sizeof(entry.href) - 1);
    entry.href[sizeof(entry.href) - 1] = '\0';
    footnotes.append(entry);
  }

  bool addLink(const char* href, int16_t x, int16_t y, int16_t width, int16_t height) {
    if (!href || width <= 0 || height <= 0 || links.size() >= MAX_LINKS_PER_PAGE) {
      return false;
    }
    const size_t hrefLen = strnlen(href, sizeof(PageLink::href));
    if (hrefLen == 0 || hrefLen == sizeof(PageLink::href)) {
      return false;
    }
    PageLink link;
    memcpy(link.href, href, hrefLen + 1);
    link.x = x;
    link.y = y;
    link.width = width;
    link.height = height;
    return links.append(link);
  }

  void render(GfxRenderer& renderer, int fontId, int xOffset, int yOffset) const;
  void renderImages(GfxRenderer& renderer, int fontId, int xOffset, int yOffset) const;
  void renderWithImagePlaceholders(GfxRenderer& renderer, int fontId, int xOffset, int yOffset) const;
  bool serialize(HalFile& file) const;
  static std::unique_ptr<Page> deserialize(HalFile& file);

  // Check if page contains any images (used to force full refresh)
  bool hasImages() const {
    return std::any_of(elements.begin(), elements.end(),
                       [](const std::unique_ptr<PageElement>& el) { return el->getTag() == TAG_PageImage; });
  }

  bool hasImagesNeedingDecode() const {
    return std::any_of(elements.begin(), elements.end(), [](const std::unique_ptr<PageElement>& element) {
      return element->getTag() == TAG_PageImage &&
             static_cast<const PageImage&>(*element).getImageBlock().needsDecode();
    });
  }

  // Get bounding box of all images on the page (union of image rects)
  // Returns false if no images. Coordinates are relative to page origin.
  bool getImageBoundingBox(int16_t& outX, int16_t& outY, int16_t& outW, int16_t& outH) const {
    bool found = false;
    int16_t minX = INT16_MAX, minY = INT16_MAX, maxX = INT16_MIN, maxY = INT16_MIN;
    for (const auto& el : elements) {
      if (el->getTag() == TAG_PageImage) {
        const auto& img = static_cast<const PageImage&>(*el);
        int16_t x = img.xPos;
        int16_t y = img.yPos;
        int16_t right = x + img.getImageBlock().getWidth();
        int16_t bottom = y + img.getImageBlock().getHeight();
        minX = std::min(minX, x);
        minY = std::min(minY, y);
        maxX = std::max(maxX, right);
        maxY = std::max(maxY, bottom);
        found = true;
      }
    }
    if (found) {
      outX = minX;
      outY = minY;
      outW = maxX - minX;
      outH = maxY - minY;
    }
    return found;
  }
};
