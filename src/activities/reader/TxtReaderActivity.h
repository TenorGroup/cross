#pragma once

#include <Txt.h>

#include <memory>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "ReaderActivity.h"

class TxtReaderActivity final : public ReaderActivity {
  std::unique_ptr<Txt> txt;

  int currentPage = 0;
  int totalPages = 1;

  // Streaming text reader - stores file offsets for each page
  std::vector<size_t> pageOffsets;
  std::vector<std::string> currentPageLines;
  int linesPerPage = 0;
  int viewportWidth = 0;
  int viewportHeight = 0;
  int cachedLineHeight = 0;
  uint8_t cachedWordSpacing = 0;
  int cachedParagraphGap = 0;
  int8_t cachedLetterSpacing = 0;
  std::vector<uint16_t> currentPageLineY;
  std::vector<uint16_t> currentPageLineIndent;
  uint16_t cachedIndent = 0;
  bool initialized = false;

  // Cached settings for cache validation
  int cachedFontId = 0;
  uint8_t cachedScreenMargin = 0;
  uint8_t cachedParagraphAlignment = CrossPointSettings::LEFT_ALIGN;
  int cachedOrientedMarginTop = 0;
  int cachedOrientedMarginRight = 0;
  int cachedOrientedMarginBottom = 0;
  int cachedOrientedMarginLeft = 0;

  void renderPage(GfxRenderer& renderer);
  void initializeReader(GfxRenderer& renderer);
  bool loadPageAtOffset(const GfxRenderer& renderer, size_t offset, std::vector<std::string>& outLines,
                        size_t& nextOffset, std::vector<uint16_t>* lineY = nullptr,
                        std::vector<uint16_t>* lineIndent = nullptr);
  void buildPageIndex(GfxRenderer& renderer);
  bool loadPageIndexCache();
  void savePageIndexCache() const;
  void saveProgress() const;
  void loadProgress();
  void renderStatusBar() const;

  bool loadBook() override;
  std::string getBookTitle() const override { return txt ? txt->getTitle() : ""; }
  void renderBook() override;

 public:
  explicit TxtReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath,
                             bool allowFastInitialRefresh)
      : ReaderActivity("TxtReader", renderer, mappedInput, std::move(bookPath), allowFastInitialRefresh) {}
  ~TxtReaderActivity() override = default;
  void onExit() override;

  bool latTrangThat(bool isForward) override;
  bool skipPages(int amount) override;
  bool docCoChuMotNac(int huong) override;
  bool isAtEndOfBook() const override;
  void onReturnFromEndOfBook() override;

  ScreenshotInfo getScreenshotInfo() const override;
};
