#include "UglyNotebook.h"

#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <Utf8.h>

#include <algorithm>
#include <cstdio>

#include "activities/home/BookStatsLibraryActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "CrossPointSettings.h"
#include "activities/home/DocThuMuc.h"
#include "FileFavorites.h"
#include "MenuCustomization.h"
#include "MenuFavorites.h"
#include "activities/home/QuotesActivity.h"
#include "activities/home/ReadingHabitsActivity.h"
#include "activities/home/ReadingHistoryActivity.h"
#include "ReadingStatsStore.h"
#include "UglyLogic.h"
#include "UglyShell.h"
#include "activities/ActivityManager.h"
#include "activities/RenderLock.h"
#include "activities/settings/SettingsActivity.h"

namespace ugly {
namespace {
constexpr int FIRST_BASELINE = 180;
constexpr int ROW_HEIGHT = 52;  // ten rows fit: the Settings page (file transfer and nine groups) is one page
constexpr int TEXT_X = 48;      // was 92: the margin line and the text hug the edge
constexpr int MARGIN_X = 26;    // was 70
constexpr int SUBTITLE_BASELINE = 126;
constexpr int SUBTITLE_LINE = 34;
constexpr StrId SUBTITLES[homerows::PAGE_COUNT] = {StrId::STR_UGLY_SUB_RECENT, StrId::STR_UGLY_SUB_FOLDER,
                                                   StrId::STR_UGLY_SUB_STATS, StrId::STR_UGLY_SUB_SETTINGS,
                                                   StrId::STR_UGLY_SUB_FAVORITES};
constexpr StrId EMPTY[homerows::PAGE_COUNT] = {StrId::STR_UGLY_EMPTY_RECENT, StrId::STR_UGLY_EMPTY_FOLDER, StrId::STR_NONE_OPT,
                                               StrId::STR_NONE_OPT, StrId::STR_UGLY_EMPTY_FAVORITES};
int id(const homerows::Page p) { return static_cast<int>(p); }
}  // namespace

int Notebook::pagePosition(const homerows::Page p) const { return menucustom::position(0, id(p), homerows::PAGE_COUNT); }

int Notebook::subtitleLines() const {
  const int room = renderer.getScreenWidth() - TEXT_X - 30;
  return std::min(2, paragraph(renderer, Size::S30, 0, 0, room, SUBTITLE_LINE, I18N.get(SUBTITLES[id(page)]), false));
}

int Notebook::firstBaseline() const { return FIRST_BASELINE + (subtitleLines() - 1) * 30; }

int Notebook::rowsPerPage() const { return std::max(1, (renderer.getScreenHeight() - 140 - firstBaseline()) / ROW_HEIGHT + 1); }

Notebook::Rows Notebook::read(const homerows::Page p) const {
  Rows r;
  switch (p) {
    case homerows::Page::Recent: {
      // The book being read is in the diary and on the desk: the page lists the others.
      auto books = homerows::recent(RecentBooksStore::MAX_RECENT_BOOKS);
      if (!books.empty()) books.erase(books.begin());
      for (const auto& b : books) r.labels.push_back(utf8ComposeNfc(b.title));
      r.books = std::move(books);
      break;
    }
    case homerows::Page::Folder: {
      // A card can hold any number of names, the heap cannot: the root is read up to a ceiling drawn from the
      // heap left, and a root past it is refused whole, with a line saying so.
      constexpr size_t BUFFER = 500;
      r.cap = logic::folderCap(ESP.getFreeHeap(), ESP.getMaxAllocHeap());
      auto buffer = makeUniqueNoThrow<char[]>(BUFFER);
      if (!buffer || r.cap == 0)
        r.tooMany = true;
      else
        docthumuc::doc("/", SETTINGS.showHiddenFiles, docthumuc::Loc::Sach, buffer.get(), BUFFER, r.folder, r.cap, &r.tooMany);
      break;
    }
    case homerows::Page::Stats:
      for (const StrId s : homerows::STATS_ROWS) r.labels.emplace_back(I18N.get(s));
      break;
    case homerows::Page::Settings: {
      r.labels.emplace_back(tr(STR_FILE_TRANSFER));
      auto groups = homerows::settingsGroups();
      r.groups = std::move(groups.ids);
      for (auto& label : groups.labels) r.labels.push_back(std::move(label));
      break;
    }
    case homerows::Page::Favorites: {
      auto pins = homerows::favorites();
      r.keys = std::move(pins.keys);
      r.values = std::move(pins.values);
      r.labels = std::move(pins.labels);
      break;
    }
  }
  return r;
}

void Notebook::adopt(Rows&& fresh) {
  rows = std::move(fresh);
  cursor[id(page)] = std::clamp(cursor[id(page)], 0, std::max(0, rowCount() - 1));
}

void Notebook::reload() {
  Rows fresh = read(page);
  RenderLock lock;
  adopt(std::move(fresh));
}

void Notebook::onEnter() {
  Screen::onEnter();
  ensureFonts(renderer);
  menucustom::load();
  adopt(read(page));
  requestUpdate();
}

void Notebook::render(RenderLock&&) {
  [[maybe_unused]] const uint32_t started = millis();
  renderer.clearScreen();
  const int w = renderer.getScreenWidth(), h = renderer.getScreenHeight();
  const int pos = pagePosition(page);

  line(renderer, MARGIN_X, 0, MARGIN_X + 1, h - 80, 501);
  const char* title = I18N.get(homerows::PAGE_TITLES[id(page)]);
  const int tw = text(renderer, Size::S52, TEXT_X, 78, title);
  underline(renderer, TEXT_X, TEXT_X + tw, 92, 17, 3);
  char number[12];
  snprintf(number, sizeof(number), "%d/%d", pos + 1, homerows::PAGE_COUNT);
  text(renderer, Size::S22, w - 30 - width(renderer, Size::S22, number), 60, number);
  paragraph(renderer, Size::S30, TEXT_X, SUBTITLE_BASELINE, w - TEXT_X - 30, SUBTITLE_LINE, I18N.get(SUBTITLES[id(page)]));
  const int first = firstBaseline();

  const int count = rowCount();
  const int perPage = rowsPerPage();
  const int cur = cursor[id(page)];
  if (rows.tooMany) {
    char said[160];
    snprintf(said, sizeof(said), tr(STR_UGLY_FOLDER_TOO_MANY), static_cast<int>(rows.cap));
    paragraph(renderer, Size::S30, TEXT_X, first, w - TEXT_X - 30, 44, said);
  } else if (count == 0) {
    const StrId empty = EMPTY[id(page)];
    if (empty != StrId::STR_NONE_OPT) paragraph(renderer, Size::S30, TEXT_X, first, w - TEXT_X - 30, 44, I18N.get(empty));
  }
  const int top = logic::pageTop(cur, perPage);
  for (int i = 0; i < perPage && top + i < count; ++i) {
    const int row = top + i;
    const int base = first + i * ROW_HEIGHT;
    int room = w - TEXT_X - 30;
    if (row < static_cast<int>(rows.values.size()) && !rows.values[row].empty()) {
      const int vw = width(renderer, Size::S22, rows.values[row].c_str());
      text(renderer, Size::S22, w - 30 - vw, base, rows.values[row].c_str());
      room -= vw + 16;
    }
    const std::string label = fit(renderer, Size::S30, labelAt(row), room);
    const int lw = text(renderer, Size::S30, TEXT_X, base, label.c_str());
    if (row == cur) circle(renderer, Circle::Row, {TEXT_X, base - 26, TEXT_X + lw, base + 8}, 12, 9);
  }
  if (count > perPage) {
    char of[24];
    snprintf(of, sizeof(of), tr(STR_UGLY_PAGE_OF), logic::pageOf(cur, perPage) + 1, logic::pageCount(count, perPage));
    text(renderer, Size::S22, w / 2 - width(renderer, Size::S22, of) / 2, h - 84, of);
  }

  // The pages next door.
  pageHints(renderer, I18N.get(homerows::PAGE_TITLES[menucustom::idAt(0, logic::cycle(pos, -1, homerows::PAGE_COUNT), homerows::PAGE_COUNT)]),
            I18N.get(homerows::PAGE_TITLES[menucustom::idAt(0, logic::cycle(pos, 1, homerows::PAGE_COUNT), homerows::PAGE_COUNT)]), h - 52);
  statusBar(renderer, mappedInput, {true, count > 0, true, true});

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
#ifdef UGLY_FRAME_LOG
  LOG_INF("UGLY", "Notebook frame page=%d row=%d rows=%d total=%lums heap=%u", id(page), cur, count,
          static_cast<unsigned long>(millis() - started), ESP.getFreeHeap());
#endif
}

// The turn of a page is decided under the lock (cheap), its rows are read after it (afterKeys): a card read
// under the lock would stop the render task for as long as the card takes.
void Notebook::afterKeys() {
  if (want == page) return;
  Rows fresh = read(want);
  {
    RenderLock lock;
    page = want;
    adopt(std::move(fresh));
  }
  requestUpdate();
}

void Notebook::activate(const int row) {
  if (row < 0 || row >= rowCount()) return;
  switch (page) {
    case homerows::Page::Recent: {
      const std::string path = rows.books[row].path;
      then([path] { activityManager.goToReader(path); });
      return;
    }
    case homerows::Page::Folder: {
      const std::string name = rows.folder[row];
      then([name] {
        if (!name.empty() && name.back() == '/')
          activityManager.goToFileBrowser("/" + name.substr(0, name.size() - 1));
        else
          activityManager.goToReader("/" + name);
      });
      return;
    }
    case homerows::Page::Stats: {
      then([this, row] {
        if (row == 0) {
          startActivityForResult(makeUniqueNoThrow<ReadingHabitsActivity>(renderer, mappedInput), nullptr);
        } else if (row == 1) {
          startActivityForResult(makeUniqueNoThrow<BookStatsLibraryActivity>(renderer, mappedInput), nullptr);
        } else if (row == 2) {
          startActivityForResult(makeUniqueNoThrow<ReadingHistoryActivity>(renderer, mappedInput), nullptr);
        } else if (row == 3) {
          startActivityForResult(makeUniqueNoThrow<QuotesActivity>(renderer, mappedInput), nullptr);
        } else {
          const bool all = row == 4;
          startActivityForResult(
              makeUniqueNoThrow<ConfirmationActivity>(
                  renderer, mappedInput, I18N.get(all ? StrId::STR_STATS_RESET_ALL : StrId::STR_STATS_RESET_HABITS),
                  I18N.get(all ? StrId::STR_STATS_RESET_ALL_BODY : StrId::STR_STATS_RESET_HABITS_BODY)),
              [all](const ActivityResult& result) {
                if (!result.isCancelled) READING_STATS.resetStatistics(all);
              });
        }
      });
      return;
    }
    case homerows::Page::Settings: {
      if (row == 0) {
        then([] { activityManager.goToFileTransfer(); });
        return;
      }
      const int group = rows.groups[row - 1];
      then([this, group] {
        startActivityForResult(makeUniqueNoThrow<SettingsActivity>(renderer, mappedInput, group, true),
                               [this](const ActivityResult&) { reload(); });
      });
      return;
    }
    case homerows::Page::Favorites: {
      const std::string key = rows.keys[row];
      then([this, key] {
        if (key == "action/15") {
          activityManager.goToFileTransfer();
          return;
        }
        if (filefavorites::isFileKey(key)) {
          const auto path = filefavorites::pathFor(key);
          if (path.empty() || !Storage.exists(path.c_str())) return;
          if (key.rfind("folder/", 0) == 0)
            activityManager.goToFileBrowser(path);
          else
            activityManager.goToReader(path);
          return;
        }
        auto target = menufavorites::open(key, renderer, mappedInput);
        if (target)
          startActivityForResult(std::move(target), [this](const ActivityResult&) { reload(); });
      });
      return;
    }
  }
}

bool Notebook::onKey(const Key key) {
  // A turn in flight: keys on the page about to be left would act on rows that are going away. Back and more
  // turns still count.
  if (want != page && key != Key::Back && key != Key::Left && key != Key::Right) return false;
  const int count = rowCount();
  int& cur = cursor[id(page)];
  switch (key) {
    case Key::Up:
      cur = logic::cycle(cur, -1, count);
      return true;
    case Key::Down:
      cur = logic::cycle(cur, 1, count);
      return true;
    case Key::UpHold:
      cur = std::max(0, cur - rowsPerPage());
      return true;
    case Key::DownHold:
      cur = std::min(std::max(0, count - 1), cur + rowsPerPage());
      return true;
    case Key::Left:
    case Key::Right:
      want = static_cast<homerows::Page>(
          menucustom::idAt(0, logic::cycle(pagePosition(want), key == Key::Left ? -1 : 1, homerows::PAGE_COUNT), homerows::PAGE_COUNT));
      return false;
    case Key::Confirm:
      activate(cur);
      return false;
    case Key::Back:
      then([this] {
        activityManager.replaceActivity(deskAvailable(renderer) ? makeDesk(renderer, mappedInput)
                                                                : makeDiary(renderer, mappedInput, false));
      });
      return false;
  }
  return false;
}

}  // namespace ugly
