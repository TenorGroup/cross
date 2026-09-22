#pragma once
#include <array>
#include <string>
#include <vector>

#include "QuoteStore.h"
#include "activities/Activity.h"
#include "components/QuoteBlockLayout.h"
#include "util/ButtonNavigator.h"

// Saved quotes, read the way a quote is read anywhere else: a bar down the left, the
// words in the reading font, and under them a smaller line naming the book, the place and
// the day. Select opens the whole quote, Up and Down walk the list, and the two page hops
// the store's paging needs sit in the list as blocks of their own.
//
// This screen draws itself rather than riding the themed list: a list row has no left bar
// and only one font, which is what made the old version read like a settings menu.
class QuotesActivity final : public Activity {
 public:
  QuotesActivity(GfxRenderer& r, MappedInputManager& input) : Activity("Quotes", r, input) {}
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  // The list holds one page of the store plus the two hops, so every buffer below is
  // fixed at that size and nothing here grows with the number of saved quotes.
  static constexpr int MAX_ENTRIES = static_cast<int>(quotes::PAGE_SIZE) + 2;
  // Longest preview one block wraps. The whole quote is read in the detail view, which
  // loads it from the card again, so no entry ever holds a full-length quote.
  static constexpr size_t PREVIEW_BYTES = 240;

  struct Entry {
    enum class Kind : uint8_t { Quote, Previous, Next, Empty };
    Kind kind = Kind::Quote;
    // Already wrapped to the block width, at most quoteblock::MAX_LINES of them.
    std::vector<std::string> lines;
    std::string source;
  };

  void loadPage(const std::string& boundary, bool previous);
  void activateIndex(int index);
  void moveSelection(int direction);
  // Index of the block drawn under `y`, or -1 when the point is outside the list.
  int blockAt(int y) const;
  quoteblock::Metrics metrics() const;
  int bandTop() const;
  int bandHeight() const;

  std::vector<std::string> names;
  std::array<Entry, MAX_ENTRIES> entries;
  std::array<uint8_t, MAX_ENTRIES> lineCounts{};
  int count = 0;
  int selected = 0;
  int top = 0;
  bool hasPrevious = false, hasNext = false;
  ButtonNavigator buttonNavigator;
};
