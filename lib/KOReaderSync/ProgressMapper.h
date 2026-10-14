#pragma once
#include <Epub.h>
#include <GfxRenderer.h>

#include <memory>
#include <optional>
#include <string>

#include "CrossPointPosition.h"
#include "KOReaderSyncClient.h"

/**
 * Progress position representation.
 */
struct SavedProgressPosition {
  std::string xpath;  // XPath-like progress string
  float percentage;   // Progress percentage (0.0 to 1.0)
};

/**
 * Maps between CrossPoint and SavedProgress position formats, such as those used by KOReader.
 *
 * CrossPoint tracks position as (spineIndex, visibleTextOffset). Page number is
 * derived from the current section layout.
 * SavedProgress uses XPath-like strings + percentage.
 *
 * The section cache records page-start visible offsets during pagination. The
 * same body-text counting rules are used to generate and resolve KOReader
 * XPaths. Percentage remains metadata and a fallback only.
 */
class ProgressMapper {
 public:
  /**
   * Convert CrossPoint position to SavedProgress format.
   *
   * @param epub The EPUB book
   * @param pos CrossPoint position
   * @return SavedProgress position
   */
  static SavedProgressPosition toSavedProgress(const std::shared_ptr<Epub>& epub, const CrossPointPosition& pos);

  /**
   * Convert SavedProgress position to CrossPoint format.
   *
   * Note: The returned pageNumber may be approximate since different
   * rendering settings produce different page counts.
   *
   * @param epub The EPUB book
   * @param savedPos SavedProgress position
   * @param renderer GfxRenderer for page count estimation
   * @param currentSpineIndex Index of the currently open spine item (for density estimation)
   * @param totalPagesInCurrentSpine Total pages in the current spine item (for density estimation)
   * @return CrossPoint position
   */
  static CrossPointPosition toCrossPoint(const std::shared_ptr<Epub>& epub, const SavedProgressPosition& savedPos,
                                         GfxRenderer& renderer, int currentSpineIndex = -1,
                                         int totalPagesInCurrentSpine = 0, int fallbackTotalPages = 0);

  /**
   * Map a progress record fetched from the server onto this device's layout: the standard KOReader
   * XPath first, the server's rich page hints when that XPath cannot be applied. The framebuffer must
   * be loaned (GfxRenderer::FrameBufferLoan) by the caller. Shared by the manual and the automatic sync.
   */
  static CrossPointPosition fromRemoteProgress(const std::shared_ptr<Epub>& epub, const KOReaderProgress& progress,
                                               GfxRenderer& renderer, int currentSpineIndex, int totalPagesInCurrentSpine);

  /**
   * The upload record for a local position: document id, XPath, percentage, the rich position for the
   * CrossPoint servers and, when enabled in the settings, the book metadata (`metadataSource` may be null:
   * the file name alone is sent). Shared by the manual and the automatic sync.
   */
  static KOReaderProgress toUploadRecord(const std::string& documentHash, const SavedProgressPosition& saved,
                                         const CrossPointPosition& position, const std::string& epubPath,
                                         const Epub* metadataSource);

  /**
   * Convert a rich CrossPoint position (downloaded from a crosspoint-sync
   * server) directly to a CrossPoint position. Its standard KOReader XPath is
   * resolved to a content offset first; legacy spine/page/paragraph hints are
   * used only when that content anchor cannot be applied.
   *
   * @param xpathAlreadyTried when true, skip re-resolving rich.xpath and go straight to the
   *        legacy page hints. The caller sets this when it just resolved the identical XPath via
   *        toCrossPoint(), so retrying it here would decompress the chapter twice for nothing.
   * @return The position, or std::nullopt when the rich position cannot be
   *         applied (spine out of range, no section cache) and the caller
   *         should fall back to toCrossPoint().
   */
  static std::optional<CrossPointPosition> fromRichPosition(const std::shared_ptr<Epub>& epub,
                                                            const KOReaderRichPosition& rich, GfxRenderer& renderer,
                                                            bool xpathAlreadyTried = false);

 private:
  /**
   * Generate a fallback XPath by streaming the spine item's XHTML and resolving
   * a paragraph/text position from intra-spine progress.
   * Produces a full ancestry path such as
   * /body/DocFragment[3]/body/p[42]/text().17.
   */
  static std::string generateXPath(const std::shared_ptr<Epub>& epub, int spineIndex, float intraSpineProgress);
};
