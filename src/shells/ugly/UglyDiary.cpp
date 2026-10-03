#include "UglyDiary.h"

#include <HalDisplay.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>

#include "CrossPointState.h"
#include "ReadingStatsStore.h"
#include "UglyShell.h"
#include "activities/ActivityManager.h"

void saveAppState();  // main.cpp

namespace ugly {
namespace {
constexpr int MARGIN = 40;
constexpr int LINE = 58;
constexpr int FIRST_BASELINE = 120;

std::vector<std::string> splitWords(const char* text) {
  std::vector<std::string> out;
  std::string word;
  for (const char* p = text; *p; ++p) {
    if (*p == ' ') {
      if (!word.empty()) out.push_back(std::move(word));
      word.clear();
    } else {
      word += *p;
    }
  }
  if (!word.empty()) out.push_back(std::move(word));
  return out;
}
}  // namespace

void Diary::buildSentence() {
  const std::string title = hasBook ? fit(renderer, Size::S38, book.title, 300) : std::string();
  char text[256];
  using K = logic::DiaryKind;
  switch (kind) {
    case K::NoBook:
      snprintf(text, sizeof(text), "%s", tr(STR_UGLY_DIARY_NO_BOOK));
      break;
    case K::NoStats:
      snprintf(text, sizeof(text), tr(STR_UGLY_DIARY_NO_STATS), title.c_str());
      break;
    case K::Today:
      snprintf(text, sizeof(text), tr(STR_UGLY_DIARY_TODAY), title.c_str(), percent, minutes);
      break;
    case K::Yesterday:
      snprintf(text, sizeof(text), tr(STR_UGLY_DIARY_YESTERDAY), title.c_str(), percent, minutes);
      break;
    case K::Ago:
      snprintf(text, sizeof(text), tr(STR_UGLY_DIARY_AGO), title.c_str(), days, percent);
      break;
    case K::Before:
      snprintf(text, sizeof(text), tr(STR_UGLY_DIARY_BEFORE), title.c_str(), percent, minutes);
      break;
  }
  sentence.clear();
  // The title may hold blanks and still travels as one token: it is never split across two lines.
  const size_t at = hasBook && !title.empty() ? std::string(text).find(title) : std::string::npos;
  if (at == std::string::npos) {
    sentence = splitWords(text);
  } else {
    const std::string whole(text);
    sentence = splitWords(whole.substr(0, at).c_str());
    sentence.push_back(title);
    for (auto& w : splitWords(whole.substr(at + title.size()).c_str())) sentence.push_back(std::move(w));
  }
}

void Diary::onEnter() {
  Screen::onEnter();
  [[maybe_unused]] const uint32_t started = millis();
  ensureFonts(renderer);
  const auto books = homerows::recent(1);
  hasBook = !books.empty();
  if (hasBook) book = books[0];
  const uint32_t today = ReadingStatsStore::currentDay();
  uint32_t lastDay = 0;
  const auto& read = READING_STATS.kho.cacNgay();
  if (!read.empty()) {
    lastDay = read.back().ma;
    minutes = read.back().phut;
  }
  BookReadingRecord record;
  if (hasBook && READING_STATS.readBook(book.path, record)) percent = record.progress;
  kind = logic::diaryKind(hasBook, lastDay, today, &days);
  buildSentence();
  ask = tr(STR_UGLY_DIARY_ASK);
  readText = tr(STR_UGLY_DIARY_READ);
  orText = tr(STR_UGLY_DIARY_OR);
  swapText = I18N.get(hasBook ? StrId::STR_UGLY_DIARY_SWAP : StrId::STR_UGLY_DIARY_PICK);
  alsoText = tr(STR_UGLY_DIARY_ALSO);
  deskText = tr(STR_UGLY_DIARY_DESK);
  wordCount = 0;
  if (hasBook) words[wordCount++] = READ;
  words[wordCount++] = SWAP;
  if (deskAvailable(renderer)) words[wordCount++] = DESK;
  selected = 0;
#ifdef UGLY_FRAME_LOG
  LOG_INF("UGLY", "Diary prepare=%lums", static_cast<unsigned long>(millis() - started));
#endif
  requestUpdate();
}

void Diary::render(RenderLock&&) {
  [[maybe_unused]] const uint32_t started = millis();
  renderer.clearScreen();
  const int w = renderer.getScreenWidth();
  const int maxWidth = w - 2 * MARGIN;
  const int space = width(renderer, Size::S38, "a") / 2 + 4;

  std::vector<logic::Token> tokens;
  for (const auto& word : sentence) tokens.push_back({word.c_str(), 0, false});
  tokens.push_back({nullptr, 0, false});
  tokens.push_back({nullptr, 0, false});
  std::vector<std::string> asked = splitWords(ask.c_str());
  for (const auto& word : asked) tokens.push_back({word.c_str(), 0, false});
  if (hasBook) {
    tokens.push_back({readText.c_str(), READ, false});
    tokens.push_back({orText.c_str(), 0, false});
  }
  tokens.push_back({swapText.c_str(), SWAP, false});
  tokens.push_back({"?", 0, true});
  if (deskAvailable(renderer)) {
    tokens.push_back({nullptr, 0, false});
    tokens.push_back({nullptr, 0, false});
    tokens.push_back({alsoText.c_str(), 0, false});
    tokens.push_back({deskText.c_str(), DESK, false});
  }

  std::vector<logic::Placed> placed(tokens.size());
  logic::layout(tokens.data(), static_cast<int>(tokens.size()), maxWidth, space,
                [&](const char* t) { return width(renderer, Size::S38, t); }, placed.data());

  Box box[4] = {};
  for (size_t i = 0; i < tokens.size(); ++i) {
    if (!tokens[i].text) continue;
    const int base = FIRST_BASELINE + placed[i].line * LINE;
    const int x = MARGIN + placed[i].x;
    text(renderer, Size::S38, x, base, tokens[i].text);
    if (tokens[i].id > 0) {
      box[tokens[i].id] = {x, base - ascent(Size::S38), x + placed[i].w, base + 12};
      underline(renderer, x, x + placed[i].w, base + 12, 13u * static_cast<uint32_t>(tokens[i].id));
    }
  }
  const Word now = words[selected];
  circle(renderer, Circle::Word, box[now], 8, 16);

  const int hintY = renderer.getScreenHeight() - 52;
  text(renderer, Size::S22, 20, hintY, (std::string("< ") + tr(STR_SETTINGS_TITLE)).c_str());
  const std::string recent = std::string(tr(STR_HOME_TAB_RECENT)) + " >";
  text(renderer, Size::S22, w - 20 - width(renderer, Size::S22, recent.c_str()), hintY, recent.c_str());
  statusBar(renderer, mappedInput, {hasBook, true, true, true});

  renderer.displayBuffer(cleanInitialRefresh ? HalDisplay::FULL_REFRESH : HalDisplay::FAST_REFRESH);
#ifdef UGLY_FRAME_LOG
  LOG_INF("UGLY", "Diary frame total=%lums heap=%u", static_cast<unsigned long>(millis() - started), ESP.getFreeHeap());
#endif
  if (cleanInitialRefresh) wakeStatePending = true;
  cleanInitialRefresh = false;
}

void Diary::onTick() {
  if (wakeStatePending.exchange(false)) saveAppState();
}

void Diary::openBook() {
  const std::string path = book.path;
  then([path] { activityManager.goToReader(path); });
}

void Diary::openPage(const homerows::Page page) {
  then([this, page] { activityManager.replaceActivity(makeNotebook(renderer, mappedInput, page)); });
}

bool Diary::onKey(const Key key) {
  switch (key) {
    case Key::Up:
    case Key::UpHold:
      selected = logic::cycle(selected, -1, wordCount);
      return true;
    case Key::Down:
    case Key::DownHold:
      selected = logic::cycle(selected, 1, wordCount);
      return true;
    case Key::Right:
      openPage(homerows::Page::Recent);
      return false;
    case Key::Left:
      openPage(homerows::Page::Settings);
      return false;
    case Key::Back:
      if (hasBook) openBook();
      return false;
    case Key::Confirm:
      switch (words[selected]) {
        case READ:
          openBook();
          break;
        case SWAP:
          openPage(hasBook ? homerows::Page::Recent : homerows::Page::Folder);
          break;
        case DESK:
          then([this] { activityManager.replaceActivity(makeDesk(renderer, mappedInput)); });
          break;
      }
      return false;
  }
  return false;
}

}  // namespace ugly
