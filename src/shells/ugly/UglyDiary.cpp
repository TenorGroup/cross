#include "UglyDiary.h"

#include <HalDisplay.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>

#include "CrossPointState.h"
#include "ReadingStatsStore.h"
#include "UglyShell.h"
#include "UglySleepSet.h"
#include "activities/ActivityManager.h"

void saveAppState();  // main.cpp

namespace ugly {
namespace {
#if FREEINK_DEVICE_X4PRO
// Lines 64 px apart: each line with an underlined word is a band the finger can hit (UglyTouch.h).
constexpr int MARGIN = 32;
constexpr int LINE = 64;
constexpr int TITLE_BASELINE = 104;  // where the notebook pages write theirs (touch::TITLE_BASE)
constexpr int FIRST_BASELINE = 176;  // the sentence starts under the title and its line
constexpr int HINTS_ROOM = 100;      // the band over the Home key starts at 720; the last line stays clear of it
constexpr int WAKE_BASELINE = 78;    // the greeting of a wake, under the top band
#else
constexpr int MARGIN = 40;
constexpr int LINE = 58;
constexpr int HINTS_ROOM = 104;  // the page hints sit 52 px over the foot; the last line and the circle round it stay clear of them
constexpr int TITLE_BASELINE = 78;   // where the notebook pages write theirs
constexpr int FIRST_BASELINE = 150;  // the sentence starts under the title and its line
constexpr int WAKE_BASELINE = 34;    // the greeting of a wake, above the title
#endif
constexpr int MIN_LINE = 46;  // tighter than this and the letters of two lines meet
constexpr int WAKE_LINE = 26;

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
  if (cleanInitialRefresh) wake = sleepset::wakeSentence();  // the first frame after a wake greets the user
  titleText = tr(STR_UGLY_DIARY_TITLE);
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

  // A wake greets above the title: the title and the sentence step down for it, and keep the room after the greeting goes.
  if (!wake.empty()) {
    const int lines = paragraph(renderer, Size::S22, MARGIN, WAKE_BASELINE, maxWidth, WAKE_LINE, wake.c_str(), false);
    shift = std::max(shift, WAKE_BASELINE + (lines - 1) * WAKE_LINE + 16 - (TITLE_BASELINE - ascent(Size::S52)));
  }
  const int tw = text(renderer, Size::S52, MARGIN, TITLE_BASELINE + shift, titleText.c_str());
  underline(renderer, MARGIN, MARGIN + tw, TITLE_BASELINE + shift + 14, 17, 3);

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
    tokens.push_back({"?", 0, true});
  }

  std::vector<logic::Placed> placed(tokens.size());
  const int lines = logic::layout(tokens.data(), static_cast<int>(tokens.size()), maxWidth, space,
                                  [&](const char* t) { return width(renderer, Size::S38, t); }, placed.data());
  // A long sentence tightens the lines so the last one stays above the page hints.
  const int first = FIRST_BASELINE + shift;
  const int step = lines > 1 ? std::clamp((renderer.getScreenHeight() - HINTS_ROOM - first) / (lines - 1), MIN_LINE, LINE) : LINE;

  Box box[4] = {};
#ifdef UGLY_FRAME_LOG
  std::string last;  // the words of the last paragraph, for the frame log
  int lowest = 0;
#endif
#if FREEINK_DEVICE_X4PRO
  drawnCount = 0;
#endif
  for (size_t i = 0; i < tokens.size(); ++i) {
    if (!tokens[i].text) {
#ifdef UGLY_FRAME_LOG
      last.clear();
#endif
      continue;
    }
    const int base = first + placed[i].line * step;
#ifdef UGLY_FRAME_LOG
    lowest = std::max(lowest, base + 12);
    if (!last.empty() && !tokens[i].attach) last += ' ';
    last += tokens[i].text;
#endif
    const int x = MARGIN + placed[i].x;
    text(renderer, Size::S38, x, base, tokens[i].text);
    if (tokens[i].id > 0) {
      box[tokens[i].id] = {x, base - ascent(Size::S38), x + placed[i].w, base + 12};
      underline(renderer, x, x + placed[i].w, base + 12, 13u * static_cast<uint32_t>(tokens[i].id));
#if FREEINK_DEVICE_X4PRO
      if (drawnCount < 3) {
        drawn[drawnCount] = {base, x, x + placed[i].w};
        drawnId[drawnCount++] = static_cast<Word>(tokens[i].id);
      }
#endif
    }
  }
  if (!wake.empty()) paragraph(renderer, Size::S22, MARGIN, WAKE_BASELINE, maxWidth, WAKE_LINE, wake.c_str());
  [[maybe_unused]] const Word now = words[selected];
#if FREEINK_DEVICE_X4PRO
  // No ring to walk with: the finger goes straight to the words. Today's date, the clock and the battery on
  // top; the two pages next door over the Home key.
  char date[12] = "";
  if (const uint32_t today = ReadingStatsStore::currentDay())
    snprintf(date, sizeof(date), "%d/%d", static_cast<int>(today % 100), static_cast<int>((today / 100) % 100));
  topBar(renderer, date);
  navRow(renderer, tr(STR_SETTINGS_TITLE), nullptr, tr(STR_HOME_TAB_RECENT));
#else
  circle(renderer, Circle::Word, box[now], 8, 16);

  pageHints(renderer, tr(STR_SETTINGS_TITLE), tr(STR_HOME_TAB_RECENT), renderer.getScreenHeight() - 52);
  statusBar(renderer, mappedInput, {hasBook, true, true, true});
#endif

  renderer.displayBuffer(cleanInitialRefresh ? HalDisplay::FULL_REFRESH : HalDisplay::FAST_REFRESH);
#ifdef UGLY_FRAME_LOG
  LOG_INF("UGLY", "Diary frame total=%lums heap=%u title=\"%s\" last=\"%s\" sel=%d box=%d,%d,%d,%d bottom=%d",
          static_cast<unsigned long>(millis() - started), ESP.getFreeHeap(), titleText.c_str(), last.c_str(), static_cast<int>(now),
          box[now].x0, box[now].y0, box[now].x1, box[now].y1, lowest);
#endif
  if (cleanInitialRefresh) wakeStatePending = true;
  cleanInitialRefresh = false;
  wake.clear();
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

#if FREEINK_DEVICE_X4PRO
bool Diary::onTouch(const Key key) {
  switch (key) {
    case Key::SwipeLeft:
      openPage(homerows::Page::Recent);
      break;
    case Key::SwipeRight:
      openPage(homerows::Page::Settings);
      break;
    case Key::SwipeUp:  // no list to scroll here: up the page is up a tier, to the desk
      if (deskAvailable(renderer)) then([this] { activityManager.replaceActivity(makeDesk(renderer, mappedInput)); });
      break;
    case Key::Tap: {
      const touch::Hit hit = touch::notebookAt(touchX, touchY);
      if (hit.spot == touch::Spot::Prev) {
        openPage(homerows::Page::Settings);
        break;
      }
      if (hit.spot == touch::Spot::Next) {
        openPage(homerows::Page::Recent);
        break;
      }
      const int at = touch::wordAt(drawn, drawnCount, touchX, touchY);
      if (at < 0) break;
      for (int i = 0; i < wordCount; ++i)
        if (words[i] == drawnId[at]) selected = i;
      return onKey(Key::Confirm);
    }
    default:
      break;
  }
  return false;
}
#endif

bool Diary::onKey(const Key key) {
#if FREEINK_DEVICE_X4PRO
  if (key >= Key::Tap) return onTouch(key);
#endif
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
    default:
      break;
  }
  return false;
}

}  // namespace ugly
