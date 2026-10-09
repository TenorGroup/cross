#pragma once
#include <string>
#include <vector>

#include "RecentBooksStore.h"
#include "UglyScreen.h"
#include "activities/home/HomeRows.h"
#if FREEINK_DEVICE_X4PRO
#include "UglyTouch.h"
#include "activities/settings/SettingsActivity.h"
#endif

namespace ugly {

// Tier 3: a page of the notebook. One template for the five pages: a handwritten title with its
// underline, a sentence of abuse, then the rows with a pen circle on the chosen one. As in tenor/cross, the
// edge buttons (Key Left and Right) turn to the page next door, the front buttons (Key Up and Down) move the
// circle, a hold on a front button jumps a page of rows.
class Notebook final : public Screen {
 public:
  Notebook(GfxRenderer& renderer, MappedInputManager& mappedInput, homerows::Page page)
      : Screen("UglyNotebook", renderer, mappedInput), page(page), want(page) {}
  void onEnter() override;
  void render(RenderLock&&) override;
#if FREEINK_DEVICE_X4PRO
  HomeMenuItem zoneRoot() const override {
    constexpr HomeMenuItem ROOTS[homerows::PAGE_COUNT] = {HomeMenuItem::RECENTS, HomeMenuItem::FILE_BROWSER,
                                                          HomeMenuItem::STATS_TAB, HomeMenuItem::SETTINGS_MENU,
                                                          HomeMenuItem::FAVORITES_TAB};
    return ROOTS[static_cast<int>(page)];
  }
#endif

 protected:
  bool onKey(Key key) override;
  bool acceptsTiltTabNavigation() const override {
#if FREEINK_DEVICE_X4PRO
    return group < 0 && pop == Pop::None;
#else
    return true;
#endif
  }
  void afterKeys() override;

 private:
  // The Folder page keeps its names once, in `folder`; every other page keeps its row texts in `labels`.
  struct Rows {
    std::vector<std::string> labels, values, keys, folder;
    std::vector<RecentBook> books;
    std::vector<int> groups;
    bool tooMany = false;  // the root of the card holds more names than the heap allows: nothing is listed
    size_t cap = 0;
  };
  // Reads a page from the card. Takes no lock: it is slow, and the render task must not wait for it.
  Rows read(homerows::Page p) const;
  // Puts the rows read in place and keeps the cursor on a row. Needs no lock when no frame can be drawn
  // (onEnter); everywhere else the caller holds it.
  void adopt(Rows&& fresh);
  void reload();  // read, then adopt under the lock
  void activate(int row);
  int rowCount() const { return static_cast<int>(page == homerows::Page::Folder ? rows.folder.size() : rows.labels.size()); }
  const std::string& labelAt(int row) const { return page == homerows::Page::Folder ? rows.folder[row] : rows.labels[row]; }
  int rowsPerPage() const;
  const char* subtitle() const;  // the line of abuse the page opened with, else its own note
  int subtitleLines() const;  // the line under the title takes 1 or 2 lines, and the rows start lower after 2
  int firstBaseline() const;
  int pagePosition(homerows::Page p) const;

  homerows::Page page;
  homerows::Page want;  // the page a turn is going to; `page` follows once its rows are read
  int cursor[homerows::PAGE_COUNT] = {};
  Rows rows;
  std::string jab;  // the line of abuse the page opened with, in place of its subtitle (UglyQuip.h)

#if FREEINK_DEVICE_X4PRO
  // ---- the touch screen (UglyTouch.h): the cursor of a page is the first row it shows ----
  // A paper open over the page: values of a setting, the question before a delete or before leaving the
  // shell, or the tasks of a row held down.
  enum class Pop : uint8_t { None, Values, Ask, Shell, Tasks };
  enum class Task : uint8_t { Pin, Info, Delete };
  // Card work a touch asked for, done after the lock is released and before the frame that shows it.
  enum class Job : uint8_t { None, Pin, Info, Delete, Save, Leave, Forget };
  Pop pop = Pop::None;
  int popRow = -1;  // the row (of all rows) the paper belongs to
  touch::Paper paper;
  touch::Ask ask;
  Task tasks[3];
  int taskCount = 0;
  Job job = Job::None;
  int jobRow = -1;
  int group = -1;  // a settings group open as a page of its own
  int groupTop = 0;
  std::vector<SettingInfo> settings;  // its rows, the last row of the page opening the old screen
  std::string said;                   // a line that takes the subtitle's place until the next touch
  int listShift = 0;                  // how far the last frame pushed the rows down for a second subtitle line
  unsigned scribbles = 0;             // touch::USED_* bits, kept on the card
  bool scribblesChanged = false;
  bool showInk = false;  // draw the strokes of the last scribble back, once

  bool onTouch(Key key);
  void renderTouch();
  int& top() { return group >= 0 ? groupTop : cursor[static_cast<int>(page)]; }
  int topShown() const { return group >= 0 ? groupTop : cursor[static_cast<int>(page)]; }
  int rowsShown() const;
  int rowTopOf(int row) const;
  void turnRows(int direction);
  void openGroup(int id);
  void closeGroup();
  void tapSetting(int row, int pageRow);
  int valueCount(const SettingInfo& s) const;
  int valueNow(const SettingInfo& s) const;
  void setValue(int row, int value);
  std::string valueLabel(const SettingInfo& s, int value) const;
  // The file or folder a row stands for ("" when none), and whether it is a folder.
  std::string pathOf(int row, bool& folder) const;
  bool pinned(int row) const;
  bool onPopTouch(Key key);
  void doJob();
  static unsigned loadScribbles();
  int askLines(bool shellAsk) const;
  // `format` with a book's name, for the one line under the title: the name is cut, never the words around it.
  std::string nameLine(const char* format, const std::string& name) const;
#endif
};

}  // namespace ugly
