#pragma once

#include <HalStorage.h>
#include <expat.h>

#include <array>
#include <climits>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "Epub/FootnoteEntry.h"
#include "Epub/ParsedText.h"
#include "Epub/blocks/ImageBlock.h"
#include "Epub/blocks/TextBlock.h"
#include "Epub/css/CssParser.h"
#include "Epub/css/CssStyle.h"

class Page;
class GfxRenderer;
class Epub;

#define MAX_WORD_SIZE 200

class ChapterHtmlSlimParser {
  std::shared_ptr<Epub> epub;
  const std::string& filepath;
  GfxRenderer& renderer;
  std::function<void(std::unique_ptr<Page>, uint16_t, uint16_t, uint32_t)> completePageFn;
  std::function<void()> popupFn;  // Popup callback
  bool imagePopupFired = false;   // popupFn fired for the first image probe (single-shot)
  int depth = 0;
  int skipUntilDepth = INT_MAX;
  int boldUntilDepth = INT_MAX;
  int italicUntilDepth = INT_MAX;
  // buffer for building up words from characters, will auto break if longer than this
  // leave one char at end for null pointer
  char partWordBuffer[MAX_WORD_SIZE + 1] = {};
  int partWordBufferIndex = 0;
  bool nextWordContinues = false;  // true when next flushed word attaches to previous (inline element boundary)
  std::unique_ptr<ParsedText> currentTextBlock = nullptr;
  // Ruby text state
  static constexpr size_t MAX_RUBY_BASE_TOKENS = 256;
  static constexpr size_t MAX_RUBY_BASE_BYTES = 8192;
  static constexpr size_t MAX_BUFFERED_RUBY_BYTES = 2 * TextBlock::MAX_RUBY_ANNOTATION_BYTES;
  bool inRuby = false;
  size_t rubyBaseBytes = 0;
  size_t rubyAnnotationBytes = 0;
  size_t rubyGroupStartWordIndex = 0;
  int rubyStartWordIndex = -1;
  bool collectingRubyText = false;
  std::string rubyTextBuffer;
  std::unique_ptr<Page> currentPage = nullptr;
  int16_t currentPageNextY = 0;
  int fontId;
  float lineCompression;
  // readerSpacing::Level, not a flag: the gap after a paragraph is
  // readerSpacing::paragraphGap(extraParagraphSpacing, lineHeight) (the stored level reaches here
  // unchanged through ReaderRenderSpec::extraParagraphSpacing).
  uint8_t extraParagraphSpacing;
  // Da xep trang it nhat mot khoi chua? Dung de khoi DAU TIEN cua chuong khong nhan khoang ngan doan
  // (khoang ngan doan thuoc ranh gioi giua hai khoi, cong o dau khoi ke tiep).
  bool khoiTruocDaXepTrang = false;
  uint8_t paragraphIndentSpaces = 2;
  bool headingSizes = true;
  int8_t characterSpacing = 0;
  uint8_t wordSpacingPercent = 100;
  uint8_t paragraphAlignment;
  uint16_t viewportWidth;
  uint16_t viewportHeight;
  bool hyphenationEnabled;
  bool focusReadingEnabled;  // always false in this firmware; the CrossPoint layout code reads it
  uint8_t dropCapMode;
  bool chapterInitialPending = true;
  int dropCapBottom = 0;
  const CssParser* cssParser;
  bool embeddedStyle;
  uint8_t imageRendering;
  std::string contentBase;
  std::string imageBasePath;
  int imageCounter = 0;

  // Style tracking (replaces depth-based approach)
  struct StyleStackEntry {
    int depth = 0;
    bool hasBold = false, bold = false;
    bool hasItalic = false, italic = false;
    bool hasTextDecoration = false;
    CssTextDecoration textDecoration = CssTextDecoration::None;
    bool hasDirection = false;
    CssTextDirection direction = CssTextDirection::Ltr;
    bool setsParagraphDirection = false;
    bool hasTextAlign = false;
    CssTextAlign textAlign = CssTextAlign::Left;
    bool hasSup = false, sup = false;
    bool hasSub = false, sub = false;
    bool hasSmallCaps = false, smallCaps = false;
    bool hasWhiteSpace = false, preserveWhitespace = false;
    // Inline font size: a multiple of the parent size, or of the body size when rem.
    bool hasFontScale = false, fontScaleRem = false;
    float fontScale = 1.0f;
  };
  std::vector<StyleStackEntry> inlineStyleStack;
  std::vector<BlockStyle> blockStyleStack;  // accumulated block styles from open ancestor elements
  CssStyle currentCssStyle;
  // Open elements by depth, for descendant/child CSS selectors. Deeper elements
  // resolve without ancestors, so contextual rules simply don't match there.
  static constexpr size_t MAX_CSS_ANCESTORS = 16;
  std::array<CssAncestor, MAX_CSS_ANCESTORS> cssAncestors{};
  bool effectiveBold = false;
  bool effectiveItalic = false;
  CssTextDecoration effectiveTextDecoration = CssTextDecoration::None;
  bool effectiveDirectionDefined = false;
  CssTextDirection effectiveDirection = CssTextDirection::Ltr;
  bool effectiveTextAlignDefined = false;
  CssTextAlign effectiveTextAlign = CssTextAlign::Left;
  bool effectiveSup = false;
  bool effectiveSub = false;
  bool effectiveSmallCaps = false;
  bool effectivePreserveWhitespace = false;
  bool pendingPageBreak = false;  // set when a page-break-after element closes

  // Initial letter captured from a ::first-letter block or a leading float/initial-letter span,
  // drawn as a PageDropCap when its block is laid out.
  static constexpr size_t MAX_DROP_CAP_BYTES = 12;
  struct DropCapState {
    char text[MAX_DROP_CAP_BYTES + 1] = {};
    uint8_t length = 0;
    uint8_t codepoints = 0;
    uint8_t lines = 0;  // text lines the letter spans
    bool bold = false;
    bool firstLetterPending = false;  // capture the current block's first letter
    int spanDepth = -1;               // capturing the text of the span at this depth
  } dropCap;

  // Open elements with a CSS border or background shade. Each emits a PageBorderBox around
  // its content on every page it spans.
  struct BoxScope {
    int depth = 0;
    int16_t left = 0;
    int16_t right = 0;
    int16_t padTop = 0;  // outer edge to content, border included
    int16_t padBottom = 0;
    CssBorderSide sides[4];  // top, right, bottom, left
    bool shaded = false;
    int16_t top = -1;  // content extent on the current page
    int16_t bottom = -1;
    bool continued = false;  // sliced onto an earlier page
  };
  static constexpr size_t MAX_BOX_SCOPES = 8;

  // Page-break carry-over: a paragraph's orphan/widow lines and a heading kept with what
  // follows move to the next page instead of being left behind.
  static constexpr size_t MAX_CARRIED_LINES = 6;
  const ParsedText* layoutParagraph = nullptr;
  bool layoutParagraphHasDropCap = false;
  uint8_t paragraphLinesOnPage = 0;
  uint8_t keepWithNextLines = 0;                       // trailing heading lines on this page
  uint32_t recentLineOffsets[MAX_CARRIED_LINES] = {};  // visible offsets of this page's last lines, newest last
  uint8_t recentLineCount = 0;
  size_t linesToCarry(size_t& paragraphLines) const;
  size_t keepWithNextCarry() const;
  bool canCarry(size_t carry) const;
  void breakPageCarryingLines(size_t carry, size_t paragraphLines, uint32_t visibleOffset);
  std::vector<BoxScope> boxScopes;
  static constexpr size_t MAX_GRID_TABLE_COLUMNS = 4;
  // ponytail: buffer at most 160 tokens / 2 KB per row; larger rows use stacked flow.
  static constexpr size_t MAX_GRID_TABLE_ROW_WORDS = 160;
  static constexpr size_t MAX_GRID_TABLE_ROW_BYTES = 2048;
  int tableDepth = 0;
  bool insideTableCell = false;
  bool tableRowStacked = false;
  bool tableRowRtl = false;
  uint16_t tableRowsSpannedRemaining = 0;
  size_t tableRowTextBytes = 0;
  std::vector<std::unique_ptr<ParsedText>> tableRowCells;
  std::array<std::vector<std::unique_ptr<TextBlock>>, MAX_GRID_TABLE_COLUMNS> tableCellLines;
  std::vector<uint32_t> tableLineVisibleOffsets;
  bool listItemBulletOnly = false;  // true when currentTextBlock has only the <li> bullet
  CssBorderSide tableBorder;        // grid lines for the current table; invisible when unbordered
  // Column widths planned from a pre-scan of the table's markup; 0 columns = equal widths.
  std::array<uint16_t, MAX_GRID_TABLE_COLUMNS> tableColumnWidths{};
  uint8_t tableColumnCount = 0;
  struct TableColumnMeasure {
    uint16_t minWidth[MAX_GRID_TABLE_COLUMNS] = {};   // longest word
    uint16_t prefWidth[MAX_GRID_TABLE_COLUMNS] = {};  // longest unwrapped line
    uint8_t columns = 0;
  };

  // A font-size span that opens a block and holds all of its text sizes the whole block,
  // e.g. <p><span class="big">Chapter One</span></p>. Text after the span cancels it.
  struct InlineSizeState {
    float scale = 1.0f;
    int depth = -1;  // outermost sizing span
    bool valid = false;
    bool open = false;
  } inlineSize;

  // Tracks the innermost open <ul>/<ol> so <li> knows whether to number itself,
  // bullet itself, or (list-style-type: none) emit no marker at all. Pushed on
  // <ul>/<ol> open, popped on close, so nested lists restart their own counter
  // without disturbing the parent list's.
  struct ListContext {
    bool ordered = false;    // true for <ol>, false for <ul>
    bool styleNone = false;  // true when list-style-type: none is set on this list
    int counter = 0;         // incremented before each direct <li>; used as its number when ordered
    int depth = 0;           // parser depth at open time; matches the depth seen in endElement
                             // for the same tag, so a hidden nested list's close can't pop
                             // an outer list's context
  };
  std::vector<ListContext> listStack;

  // Anchor-to-page mapping: tracks which page each HTML id attribute lands on. A deque grows in
  // small chunks, so a long chapter never needs one large free block (a vector doubling past 512
  // anchors needs ~28 KB contiguous, and a failed allocation aborts).
  int completedPageCount = 0;
  std::deque<std::pair<std::string, uint16_t>> anchorData;
  std::string pendingAnchorId;  // deferred until after previous text block is flushed
  // Sorted anchorKey() hashes of every anchor this spine's TOC points at. Each one is a chapter
  // boundary and a jump target: it breaks the page and is kept in anchorData whatever the cap on
  // ordinary ids, because a jump that cannot find it lands on the wrong page. Shared by the
  // parsers a build resumes, so the TOC is read once per build.
  std::shared_ptr<const std::vector<uint32_t>> tocAnchors;
  size_t tocAnchorsHeld_ = SIZE_MAX;  // chapter targets in anchorData; SIZE_MAX = not counted yet
  bool isTocAnchor(const std::string& id) const;
  size_t tocAnchorsAhead();
  size_t tocAnchorsAheadBytes();
  void recordPendingAnchor();
  uint16_t xpathParagraphIndex = 0;
  uint16_t xpathListItemIndex = 0;
  // Canonical reading-position counter: zero-based Unicode codepoints in visible
  // <body> text. Token offsets flow through line breaking so every completed page
  // records the first source character it renders.
  uint32_t visibleTextOffset = 0;
  uint32_t partWordVisibleOffset = 0;
  uint32_t currentPageVisibleOffset = 0;
  bool currentPageVisibleOffsetSet = false;
  bool insideBody = false;
  bool htmlEnded_ = false;
  bool syntheticCharacterData = false;
  uint16_t nonVisibleTextDepth = 0;

  // Footnote link tracking
  bool insideFootnoteLink = false;
  int footnoteLinkDepth = -1;
  uint8_t currentFootnoteLinkId = 0;
  FootnoteEntry currentFootnote = {};
  int currentFootnoteLinkTextLen = 0;
  std::vector<std::pair<int, FootnoteEntry>> pendingFootnotes;  // <wordIndex, entry>
  int wordsExtractedInBlock = 0;
  // Set by requestStop(): the parse ends at the next check and the caller discards the block in hand.
  // failBuild() sets it too, so the stop checks of the CrossPoint paths also end a failed build.
  bool stopRequested = false;
  // True once the build failed or a stop was asked for: nothing more is laid out.
  bool halted() const { return buildFailed_ || stopRequested; }

  // Resumable parse state. The one-shot parseAndBuildPages() drives these
  // internally; the incremental section builder drives them across render ticks
  // so a large single chapter can yield between pages instead of blocking the UI
  // until the whole thing is laid out. parseFile_ and the expat parser stay alive
  // for the lifetime of the parse so it can be paused and resumed at buffer
  // boundaries.
  bool buildFailed_ = false;
  XML_Parser xmlParser_ = nullptr;
  HalFile parseFile_;
  uint32_t parseStartTime_ = 0;
  // A checkpoint is taken only after a closed text block, at an exact XML
  // event boundary. The read cursor may already be a buffer ahead of it.
  static constexpr size_t MAX_CHECKPOINT_ANCESTORS = 32;
  uint16_t checkpointPrologBytes_ = 0;
  std::string checkpointPrefix_;
  std::array<uint16_t, MAX_CHECKPOINT_ANCESTORS> checkpointPrefixEnds_{};
  std::array<bool, MAX_CHECKPOINT_ANCESTORS> checkpointAncestorSupported_{};
  size_t checkpointDepth_ = 0;
  size_t checkpointUnsupported_ = 0;
  bool checkpointAllowed_ = true;
  bool checkpointReady_ = false;
  // Set by requestCheckpoint(): the next eligible block close stops the parse, page finished or not.
  bool checkpointWanted_ = false;
  bool replayingCheckpoint_ = false;
  // One page from the middle of a chapter (restoreResumePoint): lines starting at or before the
  // text offset below replace the candidate page. Its last line opens the preview at Y=0, and
  // following elements and spacing stay on that page once the target's block is complete.
  bool holdingLines_ = false;
  uint32_t holdUntil_ = 0;
  bool xmlSuspended_ = false;
  bool finalBuffer_ = false;
  uint32_t checkpointOffset_ = 0;
  int64_t sourceOffsetBase_ = 0;
  int stepStartPages_ = 0;
#ifdef TENOR_UI_ACCEPTANCE
  uint32_t parseStepCount_ = 0;
  uint32_t parseReadCalls_ = 0;
  uint32_t parseReadBytes_ = 0;
  uint32_t parseShortReads_ = 0;
  uint32_t parseReadErrors_ = 0;
  bool parseFinishing_ = false;
#endif

  void updateEffectiveInlineStyle();
  void startNewTextBlock(const BlockStyle& blockStyle, bool paragraphEnd = true);
  void flushPendingAnchor();
  // Emits the current page and starts the next one (emitCurrentPage + createPage).
  void completeCurrentPage();
  void noteContent(int top, int bottom);
  void openBoxScope(BlockStyle& ownStyle, const CssStyle& cssStyle);
  void closeBoxScope();
  void emitBoxSegment(const BoxScope& box, bool closing);
  void armFirstLetterDropCap(const char* tagName, const std::string& classAttr, const char* idAttr);
  bool captureDropCapCodepoint(const char* bytes, int length);
  void cancelDropCapToWord();
  void layoutCurrentBlock(bool includeLastLine);
  void layoutDropCapLines(int layoutFontId, uint16_t effectiveWidth,
                          const std::function<void(std::unique_ptr<TextBlock>, uint32_t)>& emitLine,
                          bool includeLastLine);
  static uint8_t dropCapLines(const CssStyle& style);
  bool measureTableColumns(HalFile& file, TableColumnMeasure& out) const;
  void planTableColumns(const TableColumnMeasure& measure);
  void applyPendingPageBreak();
  void pushBlockStyle(const BlockStyle& accumulated);
  void flushPartWordBuffer();
  void breakTextLine(const BlockStyle& style);
  void fallbackTableRowToStacked();
  void closeTableCell();
  void finishTableRow();
  void addTableRowSeparator();
  void setCurrentPageVisibleOffset(uint32_t offset);
  void makePages(bool includeLastLine = true, bool paragraphEnd = true);
  void flushTextIfOverBudget();
  // False once the build has failed or a page could not be allocated (failBuild()).
  bool createPage();
  bool emitCurrentPage();
  void applyBlockFontScale(BlockStyle& blockStyle, const CssStyle& cssStyle, const char* tagName) const;
  // refused (when given) is set if the font system turned the size down for want of memory (REFUSED).
  int fontIdForScale(float scale, bool* refused = nullptr) const;
  int prepareBlockFont(ParsedText& text, float scale);
  static EpdFontFamily::Style fontStyleForTextDecoration(CssTextDecoration decoration);
  static void applyDirectionToEntry(StyleStackEntry& entry, const CssStyle& css);
  static void applyTextDecorationToEntry(StyleStackEntry& entry, const CssStyle& css);
  static void applyInlinePresentationToEntry(StyleStackEntry& entry, const CssStyle& css);
  static void applyVerticalAlignToEntry(StyleStackEntry& entry, const CssStyle& css);
  void pushBlockTextStyleEntry(const CssStyle& cssStyle);
  void pushDecorationStyleEntry(CssTextDecoration defaultDecoration, const CssStyle& cssStyle);
  void emitHorizontalRule(const BlockStyle& blockStyle);
  void trackCheckpointStart(const XML_Char* name, const XML_Char** atts);
  void trackCheckpointEnd(const XML_Char* name);
  // XML callbacks
  static void XMLCALL startElement(void* userData, const XML_Char* name, const XML_Char** atts);
  static void XMLCALL characterData(void* userData, const XML_Char* s, int len);
  static void XMLCALL defaultHandlerExpand(void* userData, const XML_Char* s, int len);
  static void XMLCALL endElement(void* userData, const XML_Char* name);
  static void XMLCALL processEndElement(void* userData, const XML_Char* name);

 public:
  static constexpr size_t MAX_CHECKPOINT_PREFIX = 2048;
  explicit ChapterHtmlSlimParser(
      std::shared_ptr<Epub> epub, const std::string& filepath, GfxRenderer& renderer, const int fontId,
      const float lineCompression, const uint8_t extraParagraphSpacing, const uint8_t paragraphAlignment,
      const uint16_t viewportWidth, const uint16_t viewportHeight, const bool hyphenationEnabled,
      const bool focusReadingEnabled, const uint8_t dropCapMode,
      const std::function<void(std::unique_ptr<Page>, uint16_t, uint16_t, uint32_t)>& completePageFn,
      const bool embeddedStyle, const std::string& contentBase, const std::string& imageBasePath,
      const uint8_t imageRendering = 0, std::shared_ptr<const std::vector<uint32_t>> tocAnchors = nullptr,
      const std::function<void()>& popupFn = nullptr, const CssParser* cssParser = nullptr)

      : epub(epub),
        filepath(filepath),
        renderer(renderer),
        fontId(fontId),
        lineCompression(lineCompression),
        extraParagraphSpacing(extraParagraphSpacing),
        paragraphAlignment(paragraphAlignment),
        viewportWidth(viewportWidth),
        viewportHeight(viewportHeight),
        hyphenationEnabled(hyphenationEnabled),
        focusReadingEnabled(focusReadingEnabled),
        dropCapMode(dropCapMode),
        completePageFn(completePageFn),
        popupFn(popupFn),
        cssParser(cssParser),
        embeddedStyle(embeddedStyle),
        imageRendering(imageRendering),
        contentBase(contentBase),
        imageBasePath(imageBasePath),
        tocAnchors(std::move(tocAnchors)) {}

  ~ChapterHtmlSlimParser();
  void setTextSpacing(const int8_t character, const uint8_t wordPercent) {
    characterSpacing = character;
    wordSpacingPercent = wordPercent;
  }
  void setParagraphIndentSpaces(const uint8_t spaces) { paragraphIndentSpaces = spaces; }
  // Whether h1..h6 get a size of their own (HEADING_SIZES in ReaderRenderSpec.h says what a board does).
  void setHeadingSizes(const bool on) { headingSizes = on; }

  // One-shot parse: builds every page before returning (begin + step* + finish).
  bool parseAndBuildPages();

  // Resumable parse, for the incremental section builder. Drive as:
  //   if (!beginParse()) fail;
  //   loop: switch (parseStep()) { More: keep going / yield; Done: finishParse(); Error: abortParse(); }
  // Pages are emitted via completePageFn as they complete during parseStep(), so
  // the caller can stop once enough pages are built and resume on a later tick.
  enum class ParseStatus { More, Done, Error };
  bool beginParse();
  ParseStatus parseStep();
  bool finishParse();  // flush the trailing page and tear down; false if a stage failed, on OOM or after a stop
  void abortParse();   // tear down without flushing (error / abandon)
  // Safe from completePageFn; cleanup waits until the active callback returns.
  void requestStop();

  // A safe place to start laying out from in the middle of the chapter: after a closed block, at a
  // literal closing tag, with nothing pending. The same conditions as a checkpoint, minus "a page has
  // just finished", and none of it depends on the font, size, spacing or alignment, so a point taken
  // under one layout opens the chapter under any other. prefix: the open ancestors to replay.
  struct ResumePoint {
    uint32_t offset;
    uint32_t visible;
    int imageCounter;
  };
  std::function<void(const ResumePoint&, const std::string& prefix, uint16_t prologBytes)> resumePointFn;
  // Call on a newly begun parser: replays prolog + prefix, seeks the file to the point and lays out
  // from there; the first page starts at the line holding text offset `pageStart` (see holdingLines_).
  bool restoreResumePoint(const std::string& prologAndPrefix, const ResumePoint& point, uint32_t pageStart);
  // From the chapter start, with the first page at the line holding `pageStart` (0 = as laid out).
  void startPageAt(uint32_t pageStart);

  bool hasCheckpoint() const;
  // Checkpoints normally come only after a step that finished a page. A caller that must park the
  // parser asks for one at the next eligible block close instead. False when this chapter cannot
  // checkpoint at all (unsupported encoding or prolog), so the caller need not parse on for one.
  bool requestCheckpoint();
  bool writeCheckpoint(HalFile& file);
  // Call on a newly begun parser. A failed restore requires a fresh parser.
  bool restoreCheckpoint(HalFile& file, uint16_t expectedPages);

  void failBuild();
  bool hasFailed() const { return buildFailed_; }

  void addLineToPage(std::unique_ptr<TextBlock> line, uint32_t visibleOffset);
  // True when the chapter targets still ahead (always recorded) will not fit beside the 16 KB a page build needs,
  // yet would once the reader stops the radio. The build then starves and lays out later, with more heap, instead
  // of filling the heap with the table. A table past ANCHOR_TABLE_STARVE_MAX never fits: it grows as the heap
  // allows and the per-step heap floor parks the build.
  bool tocAnchorsNeedHeap();
  // FNV-1a 32-bit hash that identifies a TOC anchor (see tocAnchors).
  static uint32_t anchorKey(const char* id, size_t length);
  const std::deque<std::pair<std::string, uint16_t>>& getAnchors() const { return anchorData; }
  // Moves the table out; the parser keeps none. Section parks it between parsers.
  std::deque<std::pair<std::string, uint16_t>> takeAnchors() { return std::move(anchorData); }

  // Byte progress of the in-flight parse, used to estimate a still-building section's total page
  // count (a giant single-spine book never fully lays out, so its real count is unknown). Valid
  // between beginParse() and finishParse()/abortParse().
  size_t parseBytesConsumed() { return checkpointReady_ ? checkpointOffset_ : (parseFile_ ? parseFile_.position() : 0); }
  size_t parseTotalBytes() { return parseFile_ ? parseFile_.size() : 0; }
};
