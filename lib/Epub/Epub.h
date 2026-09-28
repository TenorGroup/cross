#pragma once

#include <Print.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "Epub/BookMetadataCache.h"
#include "Epub/css/CssParser.h"

class ZipFile;

class Epub {
  // the ncx file (EPUB 2)
  std::string tocNcxItem;
  // the nav file (EPUB 3)
  std::string tocNavItem;
  // where is the EPUBfile?
  std::string filepath;
  // the base path for items in the EPUB file
  std::string contentBasePath;
  // Uniq cache key based on filepath
  std::string cachePath;
  // Spine and TOC cache
  std::unique_ptr<BookMetadataCache> bookMetadataCache;
  // CSS parser for styling
  std::unique_ptr<CssParser> cssParser;
  // CSS files
  std::vector<std::string> cssFiles;

  bool findContentOpfFile(std::string* contentOpfFile, ZipFile* sharedZip = nullptr) const;
  // `metadataOnly` stops at the end of <metadata> (title, author, language). `sharedZip` reads
  // through an already open zip instead of opening the book per item.
  bool parseContentOpf(BookMetadataCache::BookMetadata& bookMetadata, bool writeSpineEntries = true,
                       bool metadataOnly = false, ZipFile* sharedZip = nullptr);
  bool parseTocNcxFile(BookMetadataCache* target, BookMetadataCache::StopFn stop) const;
  bool parseTocNavFile(BookMetadataCache* target, BookMetadataCache::StopFn stop) const;
  void discoverCssFilesFromZip();
  // A book loaded from book.part takes back where its TOC lives from there.
  void restoreTocSource();
  CssParser::ParseResult parseCssFiles(CssParser::CacheStatus existingCacheStatus) const;

 public:
  explicit Epub(std::string filepath, const std::string& cacheDir) : filepath(std::move(filepath)) {
    // create a cache key based on the filepath
    cachePath = cacheDir + "/epub_" + std::to_string(std::hash<std::string>{}(this->filepath));
  }
  ~Epub() = default;
  std::string& getBasePath() { return contentBasePath; }
  // A book of thousands of chapters (BookMetadataCache::indexesInBackground) loads with its
  // chapters and metadata only the first time: indexComplete() is false and indexSome() builds
  // the TOC and chapter sizes afterwards.
  bool load(bool buildIfMissing = true, bool skipLoadingCss = false);
  // False while the TOC and the chapter sizes are still missing: the book has no TOC entries,
  // and getBookSize() and every cumulative size answer 0, which is not the book's progress.
  bool indexComplete() const;
  enum class IndexStep : uint8_t {
    Done,     // the whole index is loaded
    More,     // a step finished; call again
    Stopped,  // `stop` answered true; the step's work is dropped and redone by the next call
    Failed,   // the step could not run (memory, card); the next call tries it again
  };
  // Runs the next step of an unfinished index: the TOC pass, then book.bin, then its load. Every
  // step leaves either its whole result on the card or nothing, so a stop, a power cut or a
  // reopen at any point resumes at a step boundary. `stop` is asked every few milliseconds.
  IndexStep indexSome(BookMetadataCache::StopFn stop);
  // Title and author from the book's cache, or from content.opf alone when it has none.
  bool loadMetadata(std::string& title, std::string& author);
  bool clearCache() const;
  void setupCacheDir() const;
  const std::string& getCachePath() const;
  const std::string& getPath() const;
  const std::string& getTitle() const;
  const std::string& getAuthor() const;
  const std::string& getLanguage() const;
  const std::string& getCoverHref() const;
  // `oneBit`: the black and white cover a folded X3 sleep screen shows (originalThresholds unused).
  std::string getCoverBmpPath(bool cropped = false, bool originalThresholds = false, bool oneBit = false) const;
  bool generateCoverBmp(bool cropped = false, bool originalThresholds = false, bool oneBit = false) const;
  std::string getThumbBmpPath() const;
  std::string getThumbBmpPath(int height) const;
  // Writes the 1-bit cover thumbnail for each height not on the card yet.
  void generateThumbBmps(const int* heights, int count) const;
  // The same from the cover's path in the book, for a book whose index is not loaded (Home).
  void generateThumbBmps(const std::string& coverImageHref, const int* heights, int count) const;
  bool isCoverImage(const std::string& href) const;
  // One height (see generateThumbBmps); true when a drawable thumbnail is on the card.
  bool generateThumbBmp(int height) const;
  // Locate the cover without building spine, TOC, or reading caches.
  bool generateThumbBmpFromSource(int height);
  uint8_t* readItemContentsToBytes(const std::string& itemHref, size_t* size = nullptr,
                                   bool trailingNullByte = false) const;
  bool readItemContentsToStream(const std::string& itemHref, Print& out, size_t chunkSize,
                                bool allowEarlyStop = false) const;
  // Extract an item to a file on SD. On failure the partial file is removed.
  bool extractItemToFile(const std::string& itemHref, const std::string& destPath) const;
  bool getItemSize(const std::string& itemHref, size_t* size) const;
  BookMetadataCache::SpineEntry getSpineItem(int spineIndex) const;
  BookMetadataCache::TocEntry getTocItem(int tocIndex) const;
  int getSpineItemsCount() const;
  int getTocItemsCount() const;
  std::unique_ptr<BookMetadataCache::TocCursor> openTocCursor(int start = 0) const {
    return bookMetadataCache ? bookMetadataCache->openTocCursor(start) : nullptr;
  }
  int getSpineIndexForTocIndex(int tocIndex) const;
  int getTocIndexForSpineIndex(int spineIndex) const;
  size_t getCumulativeSpineItemSize(int spineIndex) const;
  int getSpineIndexForTextReference() const;

  size_t getBookSize() const;
  float calculateProgress(int currentSpineIndex, float currentSpineRead) const;
  CssParser* getCssParser() const { return cssParser.get(); }
  int resolveHrefToSpineIndex(const std::string& href) const;
};
