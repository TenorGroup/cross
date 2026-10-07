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
#include "UglyQuip.h"
#include "UglyShell.h"
#include "activities/ActivityManager.h"
#include "activities/RenderLock.h"
#include "activities/settings/SettingsActivity.h"
#if FREEINK_DEVICE_X4PRO
#include "SettingsList.h"
#include "UglyTouch.h"
#include "shells/Shell.h"
#include "util/BookCacheUtils.h"
#endif

namespace ugly {
namespace {
constexpr int FIRST_BASELINE = 180;
constexpr int ROW_HEIGHT = 52;  // ten rows fit: the Settings page (file transfer and nine groups) is one page
constexpr int TEXT_X = 48;      // was 92: the margin line and the text hug the edge
constexpr int MARGIN_X = 26;    // was 70
constexpr int SUBTITLE_BASELINE = 126;
constexpr int SUBTITLE_LINE = 34;
constexpr int SUBTITLE_INDENT = 24;  // the note under the title sits further in than the rows, so it does not read as one
constexpr int SUBTITLE_EDGE = 6;     // and runs out to the edge, so a note breaks where it broke before the indent
constexpr StrId SUBTITLES[homerows::PAGE_COUNT] = {StrId::STR_UGLY_SUB_RECENT, StrId::STR_UGLY_SUB_FOLDER,
                                                   StrId::STR_UGLY_SUB_STATS, StrId::STR_UGLY_SUB_SETTINGS,
                                                   StrId::STR_UGLY_SUB_FAVORITES};
constexpr StrId EMPTY[homerows::PAGE_COUNT] = {StrId::STR_UGLY_EMPTY_RECENT, StrId::STR_UGLY_EMPTY_FOLDER, StrId::STR_NONE_OPT,
                                               StrId::STR_NONE_OPT, StrId::STR_UGLY_EMPTY_FAVORITES};
int id(const homerows::Page p) { return static_cast<int>(p); }
}  // namespace

int Notebook::pagePosition(const homerows::Page p) const { return menucustom::position(0, id(p), homerows::PAGE_COUNT); }

const char* Notebook::subtitle() const { return jab.empty() ? I18N.get(SUBTITLES[id(page)]) : jab.c_str(); }

int Notebook::subtitleLines() const {
  const int room = renderer.getScreenWidth() - TEXT_X - SUBTITLE_INDENT - SUBTITLE_EDGE;
  return std::min(2, paragraph(renderer, Size::S30, 0, 0, room, SUBTITLE_LINE, subtitle(), false));
}

int Notebook::firstBaseline() const { return FIRST_BASELINE + (subtitleLines() - 1) * 30; }

int Notebook::rowsPerPage() const {
#if FREEINK_DEVICE_X4PRO
  return touch::ROWS;
#else
  return std::max(1, (renderer.getScreenHeight() - 140 - firstBaseline()) / ROW_HEIGHT + 1);
#endif
}

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
  std::string noted = takeNoted();  // a value changed on the screen this page opened says its line here
  RenderLock lock;
  adopt(std::move(fresh));
  if (!noted.empty()) jab = std::move(noted);
}

void Notebook::onEnter() {
  Screen::onEnter();
  ensureFonts(renderer);
  menucustom::load();
  adopt(read(page));
  jab = quip(Quip::OpenPage, id(page));
#if FREEINK_DEVICE_X4PRO
  scribbles = loadScribbles();
#endif
  requestUpdate();
}

void Notebook::render(RenderLock&&) {
#if FREEINK_DEVICE_X4PRO
  renderTouch();
  return;
#endif
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
  paragraph(renderer, Size::S30, TEXT_X + SUBTITLE_INDENT, SUBTITLE_BASELINE, w - TEXT_X - SUBTITLE_INDENT - SUBTITLE_EDGE, SUBTITLE_LINE,
            subtitle());
  const int first = firstBaseline();
  line(renderer, TEXT_X, first - 40, w - 30, first - 38, 611);  // a pen rule closes the note off before the first row

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
  const int top = logic::pageTop(cur, perPage, count);
  for (int i = 0; i < logic::rowsOnPage(top, perPage, count); ++i) {
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
  if (logic::pageNamed(top, perPage, count)) {
    char of[24];
    snprintf(of, sizeof(of), tr(STR_UGLY_PAGE_OF), logic::pageOf(cur, perPage, count) + 1, logic::pageCount(count, perPage));
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
#if FREEINK_DEVICE_X4PRO
  doJob();
#endif
  if (want == page) return;
  Rows fresh = read(want);
  std::string line = quip(Quip::OpenPage, id(want));
  {
    RenderLock lock;
    page = want;
    adopt(std::move(fresh));
    jab = std::move(line);
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
#if FREEINK_DEVICE_X4PRO
  said.clear();
  if (pop != Pop::None) return onPopTouch(key);
  if (key >= Key::Tap) return onTouch(key);
  if (group >= 0) {  // a group's rows are on the page: only the way back leaves it, as the swipes and the bottom band
    if (key == Key::Back) then([this] { closeGroup(); });
    return false;
  }
#endif
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
    default:
      break;
  }
  return false;
}

#if FREEINK_DEVICE_X4PRO
// ======================= the touch screen =======================
namespace {
// Which scribbles the user has drawn once: two bits, one byte on the card.
constexpr const char* SCRIBBLES_FILE = "/.crosspoint/ugly-scribbles.txt";
using touch::ROW;
constexpr int ASK_WIDTH = 400;
// The line under the title is one line: a two-line message reads with its break as a space.
std::string flat(std::string text) {
  for (char& c : text)
    if (c == '\n') c = ' ';
  return text;
}

// The pages a scribble acts on; a group of settings is none of them.
touch::Sheet sheetOf(const homerows::Page page, const int group) {
  if (group >= 0) return touch::Sheet::Other;
  switch (page) {
    case homerows::Page::Folder:
      return touch::Sheet::Folder;
    case homerows::Page::Recent:
      return touch::Sheet::Recent;
    case homerows::Page::Favorites:
      return touch::Sheet::Favorites;
    default:
      return touch::Sheet::Other;
  }
}
}  // namespace

std::string Notebook::nameLine(const char* format, const std::string& name) const {
  char line[200];
  snprintf(line, sizeof(line), format, "");
  const int room = touch::TEXT_R - touch::TEXT_X - width(renderer, Size::S22, flat(line).c_str());
  snprintf(line, sizeof(line), format, fit(renderer, Size::S22, name, std::max(48, room)).c_str());
  return line;
}

int Notebook::askLines(const bool shellAsk) const {
  return paragraph(renderer, Size::S30, 0, 0, ASK_WIDTH, 0, shellAsk ? tr(STR_UGLY_SHELL_ASK) : tr(STR_UGLY_X4_DELETE_ASK), false);
}

unsigned Notebook::loadScribbles() {
  const String raw = Storage.readFile(SCRIBBLES_FILE);
  return raw.length() ? static_cast<unsigned>(raw[0] - '0') & (touch::USED_STRIKE | touch::USED_RING) : 0u;
}

// Where a row of the page is, with the push of the subtitle the last frame had (the frame the finger is on).
int Notebook::rowTopOf(const int row) const { return touch::rowTop(row, listShift); }

int Notebook::rowsShown() const { return logic::rowsOnPage(topShown(), touch::ROWS, rowCount()); }

void Notebook::turnRows(const int direction) {
  const int count = rowCount();
  const int pages = logic::pageCount(count, touch::ROWS);
  if (pages <= 1) return;
  top() = logic::cycle(topShown() / touch::ROWS, direction, pages) * touch::ROWS;
}

// ---- a settings group as a page ----
void Notebook::openGroup(const int id) {
  std::vector<SettingInfo> list;
  for (const auto& s : getBaseSettingsList())
    if (deviceSettingsTab(s) == id && SettingsActivity::listedAsRow(s)) list.push_back(s);
  Rows fresh;
  for (const auto& s : list) {
    fresh.labels.emplace_back(I18N.get(s.nameId));
    fresh.values.push_back(s.type == SettingType::TOGGLE ? std::string() : SettingsActivity::settingValueText(s));
  }
  // Every other row of the group (Wi-Fi, keyboards, fonts, the sleep screen...) is one row away on the old screen.
  fresh.labels.emplace_back(tr(STR_UGLY_X4_REST));
  fresh.values.emplace_back();
  const std::string line =
      quip(Quip::OpenGroup, logic::quipKey(I18N.get(settingstabs::tenThe(static_cast<settingstabs::Tab>(id)), Language::VI)));
  {
    RenderLock lock;
    group = id;
    groupTop = 0;
    settings = std::move(list);
    rows = std::move(fresh);
    said = line;
  }
  requestUpdate();
}

void Notebook::closeGroup() {
  Rows fresh = read(page);
  {
    RenderLock lock;
    group = -1;
    std::vector<SettingInfo>().swap(settings);
    adopt(std::move(fresh));
  }
  requestUpdate();
}

int Notebook::valueCount(const SettingInfo& s) const {
  if (s.type == SettingType::TOGGLE) return 2;
  return static_cast<int>(s.enumStringValues.empty() ? s.enumLabels().size() : s.enumStringValues.size());
}

int Notebook::valueNow(const SettingInfo& s) const {
  return s.valuePtr ? SETTINGS.*(s.valuePtr) : s.valueGetter ? s.valueGetter() : 0;
}

std::string Notebook::valueLabel(const SettingInfo& s, const int value) const {
  if (s.type == SettingType::TOGGLE) return value ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
  if (!s.enumStringValues.empty()) return s.enumStringValues[value];
  return I18N.get(s.enumLabels()[value]);
}

void Notebook::setValue(const int row, const int value) {
  const SettingInfo& s = settings[row];
  if (s.valuePtr == &CrossPointSettings::uiTextSize) {
    if (!SettingsActivity::applyUiTextSize(renderer, static_cast<uint8_t>(value))) return;
  } else if (s.valuePtr) {
    SETTINGS.*(s.valuePtr) = static_cast<uint8_t>(value);
  } else {
    s.valueSetter(static_cast<uint8_t>(value));
  }
  if (s.type != SettingType::TOGGLE) rows.values[row] = SettingsActivity::settingValueText(s);
  job = Job::Save;
  said = quip(Quip::SetValue, valueKey(s));
}

void Notebook::tapSetting(const int row, const int pageRow) {
  const int group = this->group;
  if (row >= static_cast<int>(settings.size()) || !SettingsActivity::changesInPlace(settings[row])) {
    // The old screen of the group, for every row a touch cannot change where it stands.
    then([this, group] {
      startActivityForResult(makeUniqueNoThrow<SettingsActivity>(renderer, mappedInput, group, true),
                             [this, group](const ActivityResult&) { openGroup(group); });
    });
    return;
  }
  const SettingInfo& s = settings[row];
  if (s.valuePtr == &CrossPointSettings::uiShell) {  // the whole device changes face: ask once
    pop = Pop::Shell;
    popRow = row;
    ask = touch::placeAsk(rowTopOf(pageRow), askLines(true), rowTopOf(pageRow) + ROW / 2);
    return;
  }
  const int count = valueCount(s), now = valueNow(s);
  switch (touch::changeFor(count, settingstabs::moTrinhChon(count))) {
    case touch::Change::Tick:
    case touch::Change::Next:
      setValue(row, logic::cycle(now, 1, count));
      break;
    case touch::Change::Paper:
      pop = Pop::Values;
      popRow = row;
      paper = touch::placePaper(rowTopOf(pageRow), count, now);
      break;
  }
}

// ---- rows that are files ----
std::string Notebook::pathOf(const int row, bool& folder) const {
  folder = false;
  if (row < 0 || row >= rowCount()) return {};
  if (page == homerows::Page::Folder) {
    const std::string& name = rows.folder[row];
    folder = !name.empty() && name.back() == '/';
    return "/" + (folder ? name.substr(0, name.size() - 1) : name);
  }
  if (page == homerows::Page::Recent && group < 0) return rows.books[row].path;
  return {};
}

bool Notebook::pinned(const int row) const {
  if (page == homerows::Page::Favorites) return true;
  bool folder = false;
  const std::string path = pathOf(row, folder);
  return !path.empty() && menucustom::state().find(filefavorites::keyFor(path, folder).c_str()) >= 0;
}

// The card work a touch asked for: after the lock, before the frame that shows what it did.
void Notebook::doJob() {
  const Job now = job;
  const int row = jobRow;
  job = Job::None;
  if (now == Job::None) return;
  if (scribblesChanged) {
    scribblesChanged = false;
    const char bits[2] = {static_cast<char>('0' + scribbles), 0};
    Storage.mkdir("/.crosspoint");
    Storage.writeFile(SCRIBBLES_FILE, String(bits));
  }
  if (now == Job::Save) {
    requestUpdate();  // the value is drawn now; the save follows the frame
    SETTINGS.saveToFile();
    return;
  }
  if (now == Job::Leave) {  // the shell says its goodbye on its own page, then tenor/cross draws Home
    requestUpdateAndWait();
    SETTINGS.uiShell = static_cast<uint8_t>(shell::Kind::Cross);
    shell::changed();
    return;
  }
  bool folder = false;
  const std::string path = pathOf(row, folder);
  const std::string name = row >= 0 && row < rowCount() ? labelAt(row) : std::string();
  char line[200] = "";
  bool reread = false;
  if (now == Job::Pin) {
    RenderLock lock;
    if (page == homerows::Page::Favorites) {
      const std::string& key = rows.keys[row];
      const bool off = filefavorites::isFileKey(key) ? filefavorites::unpin(key) : menucustom::togglePin(key.c_str());
      if (off) snprintf(line, sizeof(line), "%s", nameLine(tr(STR_UGLY_X4_UNPINNED), name).c_str());
      reread = off;
    } else if (!path.empty()) {
      const bool was = pinned(row);
      if (filefavorites::toggle(path, folder)) {
        snprintf(line, sizeof(line), "%s", nameLine(was ? tr(STR_UGLY_X4_UNPINNED) : tr(STR_UGLY_X4_PINNED), name).c_str());
        // Once both scribbles are known, the line that taught how to undo it gives way to abuse.
        const std::string mock = touch::teachScribbles(scribbles) ? std::string() : quip(was ? Quip::Unpin : Quip::Pin, 0, 0, 0, name.c_str());
        if (!mock.empty()) snprintf(line, sizeof(line), "%s", mock.c_str());
      } else {
        snprintf(line, sizeof(line), "%s", tr(STR_UGLY_X4_PIN_FULL));
      }
    }
  } else if (now == Job::Forget && !path.empty()) {
    // Off the list of books read, the book and its place in it stay on the card.
    reread = RECENT_BOOKS.removeByPath(path);
    if (reread) {
      RECENT_BOOKS.saveToFile();
      snprintf(line, sizeof(line), "%s", nameLine(tr(STR_UGLY_X4_FORGOTTEN), name).c_str());
    }
  } else if (now == Job::Info && !path.empty()) {
    HalFile f;
    const bool open = !folder && Storage.openFileForRead("UGLY", path.c_str(), f);
    snprintf(line, sizeof(line), tr(STR_UGLY_X4_INFO_LINE), name.c_str(), open ? static_cast<int>((f.size() + 1023) / 1024) : 0);
    if (open) f.close();
  } else if (now == Job::Delete && !path.empty() && !folder) {
    clearBookCache(path);
    const bool gone = Storage.remove(path.c_str());
    snprintf(line, sizeof(line), "%s", nameLine(gone ? tr(STR_UGLY_X4_DELETED) : tr(STR_UGLY_X4_DELETE_FAIL), name).c_str());
    const std::string mock = gone ? quip(Quip::Delete, 0, 0, 0, name.c_str()) : std::string();
    if (!mock.empty()) snprintf(line, sizeof(line), "%s", mock.c_str());
    reread = gone;
  }
  Rows fresh;
  if (reread) fresh = read(page);
  {
    RenderLock lock;
    if (reread) adopt(std::move(fresh));
    top() = std::min(topShown(), logic::pageTop(rowCount() - 1, touch::ROWS, rowCount()));
    said = line;
  }
  requestUpdate();
}

// ---- touches on the page ----
bool Notebook::onTouch(const Key key) {
  if (want != page) return false;
  const bool lists = sheetOf(page, group) != touch::Sheet::Other;
  switch (key) {
    case Key::SwipeLeft:
      return group < 0 && onKey(Key::Right);
    case Key::SwipeRight:
      return group < 0 && onKey(Key::Left);
    case Key::SwipeUp:
      turnRows(1);
      return true;
    case Key::SwipeDown:
      turnRows(-1);
      return true;
    case Key::Tap: {
      const touch::Hit hit = touch::notebookAt(touchX, touchY, listShift);
      switch (hit.spot) {
        case touch::Spot::Row:
          if (hit.row >= rowsShown()) return false;
          if (group >= 0)
            tapSetting(topShown() + hit.row, hit.row);
          else
            activate(topShown() + hit.row);
          return pop != Pop::None;  // a value changed is drawn by its job, after the lock
        case touch::Spot::Foot:
          if (rowsShown() > touch::ROWS) {  // the last page's lone row stands in the foot line
            if (group >= 0)
              tapSetting(topShown() + touch::ROWS, touch::ROWS);
            else
              activate(topShown() + touch::ROWS);
            return pop != Pop::None;
          }
          turnRows(1);
          return logic::pageCount(rowCount(), touch::ROWS) > 1;
        case touch::Spot::Prev:
          return group < 0 && onKey(Key::Left);
        case touch::Spot::Next:
          return group < 0 && onKey(Key::Right);
        case touch::Spot::Back:
          return onKey(Key::Back);
        case touch::Spot::None:
          return false;
      }
      return false;
    }
    case Key::Hold: {
      const int pageRow = touch::scribbleRow(touchX, touchY, rowsShown(), listShift);  // a row, or the lone row in the foot
      if (!lists || pageRow < 0) return false;
      const int row = topShown() + pageRow;
      bool folder = false;
      taskCount = 0;
      tasks[taskCount++] = Task::Pin;
      if (!pathOf(row, folder).empty() && !folder) tasks[taskCount++] = Task::Info;
      if (page == homerows::Page::Folder && !folder) tasks[taskCount++] = Task::Delete;
      pop = Pop::Tasks;
      popRow = row;
      paper = touch::placePaper(rowTopOf(pageRow), taskCount, 0);
      return true;
    }
    case Key::Scrawl:  // nobody can read it: abuse, a new line each time
      showInk = true;
      said = quip(Quip::Scrawl);
      return true;
    case Key::Strike:
    case Key::Ring: {
      if (!lists) return false;
      showInk = true;
      const int pageRow = touch::scribbleRow(touchX, touchY, rowsShown(), listShift);
      if (pageRow < 0) {
        said = quip(Quip::Scrawl);
        return true;
      }
      const int row = topShown() + pageRow;
      const unsigned used = key == Key::Strike ? touch::USED_STRIKE : touch::USED_RING;
      if (!(scribbles & used)) {
        scribbles |= used;
        scribblesChanged = true;
      }
      bool folder = false;
      const bool file = !pathOf(row, folder).empty() && !folder;
      const touch::Act act =
          touch::scribbleAct(sheetOf(page, group), key == Key::Ring ? touch::Mark::Keep : touch::Mark::Erase, file, pinned(row));
      if (act == touch::Act::Pin || act == touch::Act::Unpin || act == touch::Act::Forget) {
        job = act == touch::Act::Forget ? Job::Forget : Job::Pin;  // on Favorites the pin job takes the row off
        jobRow = row;
        return false;  // the frame follows the card work
      }
      if (act == touch::Act::Kept) {
        said = nameLine(tr(STR_UGLY_X4_KEPT), labelAt(row));
        return true;
      }
      if (act != touch::Act::AskDelete) {
        said = tr(STR_UGLY_X4_NOT_HERE);
        return true;
      }
      // Ask before a delete, "bin it" a row clear of where the finger lifted.
      const Ink& last = ink[inkCount ? inkCount - 1 : 0];
      const int liftY = last.n ? last.y[last.n - 1] : touchY;
      pop = Pop::Ask;
      popRow = row;
      ask = touch::placeAsk(rowTopOf(pageRow), askLines(false), liftY);
      return true;
    }
    default:
      return false;
  }
}

// ---- touches while a paper is open ----
bool Notebook::onPopTouch(const Key key) {
  if (key == Key::Back) {
    pop = Pop::None;
    return true;
  }
  if (key != Key::Tap) return false;
  const int row = popRow;
  switch (pop) {
    case Pop::Values: {
      const int count = valueCount(settings[row]);
      int value = -1;
      switch (touch::paperAt(paper, touchX, touchY, value)) {
        case touch::PaperSpot::Value:
          pop = Pop::None;
          if (value == valueNow(settings[row])) return true;
          setValue(row, value);
          return job == Job::None;  // else its job draws the frame
        case touch::PaperSpot::MoreAbove:
        case touch::PaperSpot::MoreBelow:
          paper = touch::scrollPaper(paper, count, touch::paperAt(paper, touchX, touchY, value) == touch::PaperSpot::MoreBelow);
          return true;
        case touch::PaperSpot::Outside:
          pop = Pop::None;
          return true;
        case touch::PaperSpot::Inside:
          return false;
      }
      return false;
    }
    case Pop::Tasks: {
      int value = -1;
      const auto spot = touch::paperAt(paper, touchX, touchY, value);
      if (spot == touch::PaperSpot::Inside) return false;
      pop = Pop::None;
      if (spot != touch::PaperSpot::Value) return true;
      switch (tasks[value]) {
        case Task::Pin:
          job = Job::Pin;
          jobRow = row;
          return false;
        case Task::Info:
          job = Job::Info;
          jobRow = row;
          return false;
        case Task::Delete:
          pop = Pop::Ask;
          popRow = row;
          ask = touch::placeAsk(rowTopOf(row - topShown()), askLines(false), touchY);
          return true;
      }
      return true;
    }
    case Pop::Ask:
    case Pop::Shell: {
      const auto spot = touch::askAt(ask, touchX, touchY);
      if (spot == touch::AskSpot::Inside) return false;
      const Pop was = pop;
      pop = Pop::None;
      if (spot != touch::AskSpot::Yes) return true;
      if (was == Pop::Ask) {
        job = Job::Delete;
        jobRow = row;
        return false;
      }
      said = quip(Quip::ShellCross);
      job = Job::Leave;
      return false;
    }
    case Pop::None:
      break;
  }
  return false;
}

// ---- the frame ----
void Notebook::renderTouch() {
  [[maybe_unused]] const uint32_t started = millis();
  renderer.clearScreen();
  const int pos = pagePosition(page);
  line(renderer, touch::MARGIN_X + 2, 0, touch::MARGIN_X + 3, touch::NAV_TOP - 8, 501);
  const char* title = I18N.get(homerows::PAGE_TITLES[id(page)]);
  const int tw = text(renderer, Size::S52, touch::TEXT_X, touch::TITLE_BASE, title);
  underline(renderer, touch::TEXT_X, touch::TEXT_X + tw, touch::TITLE_BASE + 12, 17, 3);
  if (group < 0) {
    char number[12];
    snprintf(number, sizeof(number), "%d/%d", pos + 1, homerows::PAGE_COUNT);
    text(renderer, Size::S22, touch::TEXT_R - width(renderer, Size::S22, number), touch::TITLE_BASE - 16, number);
  }
  const bool lists = sheetOf(page, group) != touch::Sheet::Other;
  const char* sub = !said.empty() ? said.c_str()
                    : group >= 0 ? I18N.get(settingstabs::tenThe(static_cast<settingstabs::Tab>(group)))
                    : touch::hintHolds(sheetOf(page, group)) && touch::teachScribbles(scribbles) ? tr(STR_UGLY_X4_HINT_GESTURE)
                                                                : nullptr;
  if (!sub && !jab.empty()) sub = jab.c_str();
  if (!sub) sub = I18N.get(SUBTITLES[id(page)]);

  const int count = rowCount();
  const int first = topShown();
  // A two-line subtitle takes a second line where the page's rows leave room for it; a paper open keeps the layout it
  // was opened on, and the rows stay where the finger saw them.
  const char* cut = std::strchr(sub, '\n');
  if (pop == Pop::None) listShift = touch::subShift(logic::rowsOnPage(first, touch::ROWS, count), cut && cut[1]);
  const int room = touch::TEXT_R - touch::TEXT_X;
  if (listShift && cut && cut[1]) {
    text(renderer, Size::S22, touch::TEXT_X, touch::SUB_BASE, fit(renderer, Size::S22, std::string(sub, cut - sub), room).c_str());
    text(renderer, Size::S22, touch::TEXT_X, touch::SUB_BASE + touch::SUB_LINE2, fit(renderer, Size::S22, flat(cut + 1), room).c_str());
  } else {
    text(renderer, Size::S22, touch::TEXT_X, touch::SUB_BASE, fit(renderer, Size::S22, flat(sub), room).c_str());
  }
  if (rows.tooMany) {
    char tooMany[160];
    snprintf(tooMany, sizeof(tooMany), tr(STR_UGLY_FOLDER_TOO_MANY), static_cast<int>(rows.cap));
    paragraph(renderer, Size::S30, touch::TEXT_X, rowTopOf(0) + 42, touch::TEXT_R - touch::TEXT_X, 44, tooMany);
  } else if (count == 0) {
    const StrId empty = EMPTY[id(page)];
    if (empty != StrId::STR_NONE_OPT)
      paragraph(renderer, Size::S30, touch::TEXT_X, rowTopOf(0) + 42, touch::TEXT_R - touch::TEXT_X, 44, I18N.get(empty));
  }
  for (int i = 0; i < logic::rowsOnPage(first, touch::ROWS, count); ++i) {
    const int row = first + i, base = rowTopOf(i) + 42;
    int room = touch::TEXT_R - touch::TEXT_X;
    if (group >= 0 && row < static_cast<int>(settings.size()) && settings[row].type == SettingType::TOGGLE) {
      tickBox(renderer, touch::TEXT_R - 4, rowTopOf(i) + 30, valueNow(settings[row]) != 0);
      room -= 40;
    } else if (row < static_cast<int>(rows.values.size()) && !rows.values[row].empty()) {
      const int vw = width(renderer, Size::S22, rows.values[row].c_str());
      text(renderer, Size::S22, touch::TEXT_R - vw, base, rows.values[row].c_str());
      room -= vw + 16;
    } else if (lists && pinned(row)) {
      heart(renderer, touch::TEXT_R - 14, rowTopOf(i) + 32);
      room -= 40;
    }
    text(renderer, Size::S30, touch::TEXT_X, base, fit(renderer, Size::S30, labelAt(row), room).c_str());
  }
  if (logic::pageNamed(first, touch::ROWS, count)) {
    char of[24];
    snprintf(of, sizeof(of), tr(STR_UGLY_PAGE_OF), first / touch::ROWS + 1, logic::pageCount(count, touch::ROWS));
    const int ow = width(renderer, Size::S22, of);
    text(renderer, Size::S22, 240 - ow / 2, touch::FOOT_TOP + 36, of);
    arrow(renderer, 240 + ow / 2 + 16, touch::FOOT_TOP + 28, true, 18);
  }
  if (group >= 0) {
    char back[48];
    snprintf(back, sizeof(back), tr(STR_UGLY_X4_BACK_PARENT), title);
    navRow(renderer, nullptr, back, nullptr);
  } else {
    const auto neighbour = [&](const int step) {
      return I18N.get(homerows::PAGE_TITLES[menucustom::idAt(0, logic::cycle(pos, step, homerows::PAGE_COUNT), homerows::PAGE_COUNT)]);
    };
    navRow(renderer, neighbour(-1), tr(STR_UGLY_X4_BACK_DESK), neighbour(1));
  }
  topBar(renderer, nullptr);

  if (showInk) {  // the scribble, drawn back once with what it did
    for (int k = 0; k < inkCount; ++k) penPath(renderer, ink[k].x, ink[k].y, ink[k].n);
    showInk = false;
  }

  switch (pop) {
    case Pop::Values:
    case Pop::Tasks: {
      const bool values = pop == Pop::Values;
      ugly::paper(renderer, paper.top, paper.bottom, paper.hiddenAbove > 0, paper.hiddenBelow > 0, 750, listShift);
      const std::string label = (values ? std::string(I18N.get(settings[popRow].nameId)) : labelAt(popRow)) + ":";
      char more[32];
      const touch::Paper& p = paper;
      const std::string head = fit(renderer, Size::S22, label, 380);
      if (p.hiddenAbove) {
        text(renderer, Size::S22, 44, p.top + 28, head.c_str());
        arrow(renderer, 56, p.top + 46, false, 16);
        snprintf(more, sizeof(more), tr(STR_UGLY_MORE), p.hiddenAbove);
        text(renderer, Size::S22, 76, p.top + 54, more);
      } else {
        text(renderer, Size::S22, 44, p.top + 30, head.c_str());
      }
      const int now = values ? valueNow(settings[popRow]) : -1;
      for (int k = p.first; k <= p.last; ++k) {
        const int rt = touch::paperRowTop(p, k);
        std::string option;
        if (values) {
          option = valueLabel(settings[popRow], k);
        } else {
          const bool on = pinned(popRow);
          option = tasks[k] == Task::Pin ? (on ? tr(STR_UGLY_X4_UNPIN) : tr(STR_UGLY_X4_PIN))
                   : tasks[k] == Task::Info ? tr(STR_UGLY_X4_INFO)
                                            : tr(STR_UGLY_X4_DELETE);
        }
        const std::string shown = fit(renderer, Size::S30, option, 360);
        const int ow = text(renderer, Size::S30, 80, rt + 42, shown.c_str());
        if (k == now) circle(renderer, Circle::Row, {80, rt + 16, 80 + ow, rt + 50}, 14, 9);
      }
      if (p.hiddenBelow) {
        const int yb = p.bottom - touch::PAPER_MORE;
        arrow(renderer, 56, yb + 30, true, 20);
        snprintf(more, sizeof(more), tr(STR_UGLY_MORE), p.hiddenBelow);
        text(renderer, Size::S22, 76, yb + 38, more);
      }
      break;
    }
    case Pop::Ask:
    case Pop::Shell: {
      const bool shellAsk = pop == Pop::Shell;
      ugly::paper(renderer, ask.top, ask.bottom, false, false, 770, listShift);
      paragraph(renderer, Size::S30, 48, ask.textBase, ASK_WIDTH, touch::ASK_LINE,
                shellAsk ? tr(STR_UGLY_SHELL_ASK) : tr(STR_UGLY_X4_DELETE_ASK));
      text(renderer, Size::S30, 80, ask.noTop + 42, shellAsk ? tr(STR_UGLY_SHELL_NO) : tr(STR_UGLY_X4_DELETE_NO));
      const int yw = text(renderer, Size::S30, 80, ask.yesTop + 42, shellAsk ? tr(STR_UGLY_SHELL_YES) : tr(STR_UGLY_X4_DELETE));
      underline(renderer, 76, 80 + yw, ask.yesTop + 52, 775, 2);
      break;
    }
    case Pop::None:
      break;
  }

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
#ifdef UGLY_FRAME_LOG
  LOG_INF("UGLY", "Notebook frame page=%d group=%d top=%d rows=%d pop=%d total=%lums", id(page), group, first, count,
          static_cast<int>(pop), static_cast<unsigned long>(millis() - started));
#endif
}
#endif

}  // namespace ugly
