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
constexpr int TEXT_X = 92;
constexpr StrId SUBTITLES[homerows::PAGE_COUNT] = {StrId::STR_UGLY_SUB_RECENT, StrId::STR_UGLY_SUB_FOLDER,
                                                   StrId::STR_UGLY_SUB_STATS, StrId::STR_UGLY_SUB_SETTINGS,
                                                   StrId::STR_UGLY_SUB_FAVORITES};
constexpr StrId EMPTY[homerows::PAGE_COUNT] = {StrId::STR_UGLY_EMPTY_RECENT, StrId::STR_UGLY_EMPTY_FOLDER, StrId::STR_NONE_OPT,
                                               StrId::STR_NONE_OPT, StrId::STR_UGLY_EMPTY_FAVORITES};
int id(const homerows::Page p) { return static_cast<int>(p); }
}  // namespace

int Notebook::pagePosition() const { return menucustom::position(0, id(page), homerows::PAGE_COUNT); }

int Notebook::rowsPerPage() const { return std::max(1, (renderer.getScreenHeight() - 140 - FIRST_BASELINE) / ROW_HEIGHT + 1); }

void Notebook::load() {
  rows = Rows{};
  switch (page) {
    case homerows::Page::Recent: {
      // The book being read is in the diary and on the desk: the page lists the others.
      auto books = homerows::recent(RecentBooksStore::MAX_RECENT_BOOKS);
      if (!books.empty()) books.erase(books.begin());
      for (const auto& b : books) rows.labels.push_back(utf8ComposeNfc(b.title));
      rows.books = std::move(books);
      break;
    }
    case homerows::Page::Folder: {
      constexpr size_t BUFFER = 500;
      auto buffer = makeUniqueNoThrow<char[]>(BUFFER);
      if (buffer) docthumuc::doc("/", SETTINGS.showHiddenFiles, docthumuc::Loc::Sach, buffer.get(), BUFFER, rows.folder);
      for (const auto& name : rows.folder) rows.labels.push_back(utf8ComposeNfc(name));
      break;
    }
    case homerows::Page::Stats:
      for (const StrId s : homerows::STATS_ROWS) rows.labels.emplace_back(I18N.get(s));
      break;
    case homerows::Page::Settings: {
      rows.labels.emplace_back(tr(STR_FILE_TRANSFER));
      auto groups = homerows::settingsGroups();
      rows.groups = std::move(groups.ids);
      for (auto& label : groups.labels) rows.labels.push_back(std::move(label));
      break;
    }
    case homerows::Page::Favorites: {
      auto pins = homerows::favorites();
      rows.keys = std::move(pins.keys);
      rows.values = std::move(pins.values);
      rows.labels = std::move(pins.labels);
      break;
    }
  }
  const int last = static_cast<int>(rows.labels.size()) - 1;
  cursor[id(page)] = std::clamp(cursor[id(page)], 0, std::max(0, last));
}

void Notebook::onEnter() {
  Screen::onEnter();
  ensureFonts(renderer);
  menucustom::load();
  load();
  requestUpdate();
}

void Notebook::render(RenderLock&&) {
  [[maybe_unused]] const uint32_t started = millis();
  renderer.clearScreen();
  const int w = renderer.getScreenWidth(), h = renderer.getScreenHeight();
  const int pos = pagePosition();

  line(renderer, 70, 0, 71, h - 80, 501);
  const char* title = I18N.get(homerows::PAGE_TITLES[id(page)]);
  const int tw = text(renderer, Size::S52, TEXT_X, 78, title);
  underline(renderer, TEXT_X, TEXT_X + tw, 92, 17, 3);
  char number[12];
  snprintf(number, sizeof(number), "%d/%d", pos + 1, homerows::PAGE_COUNT);
  text(renderer, Size::S22, w - 30 - width(renderer, Size::S22, number), 60, number);
  text(renderer, Size::S22, TEXT_X, 124, fit(renderer, Size::S22, I18N.get(SUBTITLES[id(page)]), w - TEXT_X - 30).c_str());

  const int count = static_cast<int>(rows.labels.size());
  const int perPage = rowsPerPage();
  const int cur = cursor[id(page)];
  if (count == 0) {
    const StrId empty = EMPTY[id(page)];
    if (empty != StrId::STR_NONE_OPT) paragraph(renderer, Size::S30, TEXT_X, FIRST_BASELINE, w - TEXT_X - 30, 44, I18N.get(empty));
  }
  const int top = logic::pageTop(cur, perPage);
  for (int i = 0; i < perPage && top + i < count; ++i) {
    const int row = top + i;
    const int base = FIRST_BASELINE + i * ROW_HEIGHT;
    int room = w - TEXT_X - 30;
    if (row < static_cast<int>(rows.values.size()) && !rows.values[row].empty()) {
      const int vw = width(renderer, Size::S22, rows.values[row].c_str());
      text(renderer, Size::S22, w - 30 - vw, base, rows.values[row].c_str());
      room -= vw + 16;
    }
    const std::string label = fit(renderer, Size::S30, rows.labels[row], room);
    const int lw = text(renderer, Size::S30, TEXT_X, base, label.c_str());
    if (row == cur) circle(renderer, Circle::Row, {TEXT_X, base - 26, TEXT_X + lw, base + 8}, 12, 9);
  }
  if (count > perPage) {
    char of[24];
    snprintf(of, sizeof(of), tr(STR_UGLY_PAGE_OF), logic::pageOf(cur, perPage) + 1, logic::pageCount(count, perPage));
    text(renderer, Size::S22, w / 2 - width(renderer, Size::S22, of) / 2, h - 84, of);
  }

  // The pages next door.
  const std::string before = std::string("< ") + I18N.get(homerows::PAGE_TITLES[menucustom::idAt(0, logic::cycle(pos, -1, homerows::PAGE_COUNT), homerows::PAGE_COUNT)]);
  const std::string after = std::string(I18N.get(homerows::PAGE_TITLES[menucustom::idAt(0, logic::cycle(pos, 1, homerows::PAGE_COUNT), homerows::PAGE_COUNT)])) + " >";
  text(renderer, Size::S22, 20, h - 52, before.c_str());
  text(renderer, Size::S22, w - 20 - width(renderer, Size::S22, after.c_str()), h - 52, after.c_str());
  statusBar(renderer, mappedInput, {true, count > 0, true, true});

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
#ifdef UGLY_FRAME_LOG
  LOG_INF("UGLY", "Notebook frame page=%d row=%d rows=%d total=%lums heap=%u", id(page), cur, count,
          static_cast<unsigned long>(millis() - started), ESP.getFreeHeap());
#endif
}

void Notebook::turn(const int step) {
  const int pos = logic::cycle(pagePosition(), step, homerows::PAGE_COUNT);
  page = static_cast<homerows::Page>(menucustom::idAt(0, pos, homerows::PAGE_COUNT));
  load();
}

void Notebook::activate(const int row) {
  if (row < 0 || row >= static_cast<int>(rows.labels.size())) return;
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
                               [this](const ActivityResult&) {
                                 RenderLock lock;
                                 load();
                               });
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
          startActivityForResult(std::move(target), [this](const ActivityResult&) {
            RenderLock lock;
            load();
          });
      });
      return;
    }
  }
}

bool Notebook::onKey(const Key key) {
  const int count = static_cast<int>(rows.labels.size());
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
      turn(-1);
      return true;
    case Key::Right:
      turn(1);
      return true;
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
