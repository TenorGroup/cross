#pragma once
#include <string>
#include <vector>

#include "QuoteStore.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Trims words off either end of a saved quote (mockup E2). The words keep their places:
// kept words plain, dropped words struck through, the boundary being moved underlined.
// Left and Right move that boundary one word, a side button switches between the first
// and the last word, Select saves through quotes::trimWords (which moves the anchor with
// the words), Back leaves the quote as it was.
class QuoteTrimActivity final : public Activity {
 public:
  QuoteTrimActivity(GfxRenderer& r, MappedInputManager& input, quotes::QuoteId id)
      : Activity("QuoteTrim", r, input), id(id) {}
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  // A word is a span of quote.text, so the layout holds no copies of the words.
  struct Word {
    uint16_t start = 0;
    uint16_t length = 0;
    int16_t x = 0;
    int16_t width = 0;
    uint16_t line = 0;
  };

  void layoutWords();
  const char* wordText(const Word& word) const;
  void move(int direction);
  void save();
  void cancel();
  int areaTop() const;
  int linesPerArea() const;
  int firstShownLine() const;
  void logArea() const;

  quotes::QuoteId id;
  QuoteRecord quote;
  std::vector<Word> words;
  mutable std::string scratch;
  int lineCount = 0;
  // Kept words are [first, last]; both are word indices.
  int first = 0;
  int last = 0;
  bool movingEnd = true;
  const char* failure = nullptr;
  unsigned long failureAt = 0;
  ButtonNavigator navigator;
};
