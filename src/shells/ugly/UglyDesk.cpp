#include "UglyDesk.h"

#include <HalDisplay.h>
#include <I18n.h>
#include <Logging.h>

#include <cstdio>

#include "ReadingStatsStore.h"
#include "UglyArt.h"
#include "UglyLogic.h"
#include "UglyShell.h"
#include "activities/ActivityManager.h"
#include "components/X3BrandCodec.h"

namespace ugly {
namespace {
struct Place {
  Box box;      // where the drawing is
  StrId label;  // written under it
};
// Where the baked drawing puts each object (528x792).
constexpr Place PLACES[6] = {
    {{352, 66, 498, 236}, StrId::STR_HOME_TAB_STATS},    {{210, 80, 312, 225}, StrId::STR_HOME_TAB_RECENT},
    {{75, 286, 455, 515}, StrId::STR_UGLY_DESK_READING}, {{36, 558, 240, 668}, StrId::STR_HOME_TAB_FOLDER},
    {{322, 512, 480, 672}, StrId::STR_READER_TAB_FAVORITES}, {{36, 60, 186, 248}, StrId::STR_SETTINGS_TITLE}};
constexpr int ROW_TOLERANCE = 60;  // objects whose centres lie this close vertically share a row
constexpr homerows::Page PAGES[6] = {homerows::Page::Stats,   homerows::Page::Recent,    homerows::Page::Recent,
                                     homerows::Page::Folder,  homerows::Page::Favorites, homerows::Page::Settings};
}  // namespace

bool deskAvailable(const GfxRenderer& renderer) {
  return renderer.hasFrameBuffer() && renderer.getBufferSize() == logic::FRAME_BYTES &&
         renderer.getScreenWidth() == logic::FRAME_W && renderer.getScreenHeight() == logic::FRAME_H;
}

void Desk::onEnter() {
  Screen::onEnter();
  ensureFonts(renderer);
  const auto books = homerows::recent(1);
  hasBook = !books.empty();
  if (hasBook) {
    book = books[0];
    BookReadingRecord record;
    if (READING_STATS.readBook(book.path, record)) percent = record.progress;
  }
  requestUpdate();
}

void Desk::render(RenderLock&&) {
  [[maybe_unused]] const uint32_t started = millis();
  // The picture needs the decompressor's state (~8 KB in one block) from a heap BLE leaves fragmented. When it
  // cannot be had the desk is a bare table with the labels and the ring, and the log says how much was left.
  if (!decodeX3BrandPlane(art::DESK, sizeof(art::DESK), renderer.getFrameBuffer(), renderer.getBufferSize())) {
    LOG_ERR("UGLY", "Desk picture not drawn (plane %u B): heap=%u largest=%u", static_cast<unsigned>(renderer.getBufferSize()),
            ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    renderer.clearScreen();
  }

  // The pages' own words, centred under their objects.
  Box ring[COUNT];
  for (int i = 0; i < COUNT; ++i) {
    const auto& p = PLACES[i];
    const char* label = i == READING && !hasBook ? tr(STR_UGLY_DESK_NO_BOOK) : I18N.get(p.label);
    const int lw = width(renderer, Size::S22, label);
    const int lx = (p.box.x0 + p.box.x1) / 2 - lw / 2;
    const int base = p.box.y1 + 26;
    text(renderer, Size::S22, lx, base, label);
    ring[i] = {std::min(p.box.x0, lx), p.box.y0, std::max(p.box.x1, lx + lw), base + 8};
  }

  // The open book shows the one being read.
  if (hasBook) {
    text(renderer, Size::S30, 95, 370, fit(renderer, Size::S30, book.title, 150).c_str());
    text(renderer, Size::S22, 105, 402, fit(renderer, Size::S22, book.author, 140).c_str());
    char pct[8];
    snprintf(pct, sizeof(pct), "%d%%", percent);
    text(renderer, Size::S30, 295, 370, pct);
  }
  // The calendar shows today.
  const uint32_t today = ReadingStatsStore::currentDay();
  if (today != 0) {
    char month[24], day[8];
    snprintf(month, sizeof(month), tr(STR_UGLY_DESK_MONTH), static_cast<int>((today / 100) % 100));
    snprintf(day, sizeof(day), "%d", static_cast<int>(today % 100));
    text(renderer, Size::S22, 382, 122, month);
    text(renderer, Size::S52, 410 - (today % 100 >= 10 ? 6 : 0), 208, day);
  }

  // The big open book gets a snugger ring than the small objects.
  circle(renderer, Circle::Object, ring[selected], selected == READING ? 8 : 16, selected == READING ? 12 : 24);
  statusBar(renderer, mappedInput, {true, true, true, true});
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
#ifdef UGLY_FRAME_LOG
  LOG_INF("UGLY", "Desk frame sel=%d total=%lums plane=%uB heap=%u largest=%u", selected, static_cast<unsigned long>(millis() - started),
          static_cast<unsigned>(renderer.getBufferSize()), ESP.getFreeHeap(), ESP.getMaxAllocHeap());
#endif
}

void Desk::open() {
  if (selected == READING) {
    if (hasBook) {
      const std::string path = book.path;
      then([path] { activityManager.goToReader(path); });
    }
    return;
  }
  const auto page = PAGES[selected];
  then([this, page] { activityManager.replaceActivity(makeNotebook(renderer, mappedInput, page)); });
}

// By place, not by turn: up and down go to the row above or below, sideways stays in the row.
bool Desk::onKey(const Key key) {
  logic::Point centres[COUNT];
  for (int i = 0; i < COUNT; ++i) centres[i] = {(PLACES[i].box.x0 + PLACES[i].box.x1) / 2, (PLACES[i].box.y0 + PLACES[i].box.y1) / 2};
  logic::GridDir dir;
  switch (key) {
    case Key::Up:
    case Key::UpHold:
      dir = logic::GridDir::Up;
      break;
    case Key::Down:
    case Key::DownHold:
      dir = logic::GridDir::Down;
      break;
    case Key::Left:
      dir = logic::GridDir::Left;
      break;
    case Key::Right:
      dir = logic::GridDir::Right;
      break;
    case Key::Confirm:
      open();
      return false;
    case Key::Back:
      then([this] { activityManager.replaceActivity(makeDiary(renderer, mappedInput, false)); });
      return false;
    default:
      return false;
  }
  if (anchorX < 0) anchorX = centres[selected].x;
  const int next = logic::gridStep(centres, COUNT, selected, dir, anchorX, ROW_TOLERANCE);
  if (next == selected) return false;
  selected = next;
  if (dir == logic::GridDir::Left || dir == logic::GridDir::Right) anchorX = centres[selected].x;
  return true;
}

}  // namespace ugly
