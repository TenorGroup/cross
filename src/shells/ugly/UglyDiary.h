#pragma once
#include <atomic>
#include <string>
#include <vector>

#include "RecentBooksStore.h"
#include "UglyLogic.h"
#include "UglyScreen.h"
#include "activities/home/HomeRows.h"

namespace ugly {

// Tier 1: a few handwritten sentences. The underlined words are what the buttons walk.
class Diary final : public Screen {
 public:
  Diary(GfxRenderer& renderer, MappedInputManager& mappedInput, bool cleanInitialRefresh)
      : Screen("UglyDiary", renderer, mappedInput), cleanInitialRefresh(cleanInitialRefresh) {}
  void onEnter() override;
  void onTick() override;
  void render(RenderLock&&) override;

 protected:
  bool onKey(Key key) override;

 private:
  enum Word : int8_t { READ = 1, SWAP = 2, DESK = 3 };
  struct Paragraph {
    std::vector<std::string> words;  // plain words of the sentence
  };
  void buildSentence();
  void openBook();
  void openPage(homerows::Page page);

  bool cleanInitialRefresh;
  // The wake's first frame is up: the main task writes state.json (never the render task).
  std::atomic<bool> wakeStatePending{false};
  bool hasBook = false;
  RecentBook book;
  int percent = 0, minutes = 0, days = 0;
  logic::DiaryKind kind = logic::DiaryKind::NoBook;
  std::vector<std::string> sentence;   // words of the first paragraph
  std::string ask, readText, orText, swapText, alsoText, deskText;
  Word words[3];
  int wordCount = 0;
  int selected = 0;  // index in words[]
};

}  // namespace ugly
