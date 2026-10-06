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
#if FREEINK_DEVICE_X4PRO
#include <Memory.h>

#include <algorithm>

#include "UglyTouch.h"
#endif

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

#if FREEINK_DEVICE_X4PRO
// The touch desk is laid out on the 480x800 grid (UglyTouch.h): each object of the X3 picture is lifted
// whole, unscaled, into its own cell. `dx`, `dy` move the box the X3 picture draws it in (PLACES).
struct Lift {
  int dx, dy;
  int labelX, labelBase;  // the label, centred at labelX
};
constexpr Lift LIFTS[6] = {{-23, 9, 402, 278}, {-15, -2, 246, 266}, {-25, 6, 240, 536},
                           {-20, -1, 118, 700}, {-37, 18, 364, 706}, {-33, 4, 78, 274}};
constexpr int LIFT_PAD = 4;  // a stroke may stray this far out of its box
#endif
}  // namespace

bool deskAvailable(const GfxRenderer& renderer) {
#if FREEINK_DEVICE_X4PRO
  return renderer.hasFrameBuffer() && renderer.getScreenWidth() == touch::W && renderer.getScreenHeight() == touch::H;
#else
  return renderer.hasFrameBuffer() && renderer.getBufferSize() == logic::FRAME_BYTES &&
         renderer.getScreenWidth() == logic::FRAME_W && renderer.getScreenHeight() == logic::FRAME_H;
#endif
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

#if FREEINK_DEVICE_X4PRO
void Desk::render(RenderLock&&) {
  [[maybe_unused]] const uint32_t started = millis();
  renderer.clearScreen();
  // The X3 picture is inflated once into a scratch plane (the PSRAM holds it), then each object is lifted.
  auto plane = makeUniqueNoThrow<uint8_t[]>(logic::FRAME_BYTES);
  if (plane && decodeX3BrandPlane(art::DESK, sizeof(art::DESK), plane.get(), logic::FRAME_BYTES)) {
    for (int i = 0; i < COUNT; ++i) {
      const Box& b = PLACES[i].box;
      liftArt(renderer, plane.get(), {b.x0 - LIFT_PAD, b.y0 - LIFT_PAD, b.x1 + LIFT_PAD, b.y1 + LIFT_PAD}, LIFTS[i].dx, LIFTS[i].dy);
    }
  } else {
    LOG_ERR("UGLY", "Desk picture not drawn: heap=%u largest=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  }
  plane.reset();
  for (int i = 0; i < COUNT; ++i) {
    const char* label = i == READING && !hasBook ? tr(STR_UGLY_DESK_NO_BOOK) : I18N.get(PLACES[i].label);
    text(renderer, Size::S22, LIFTS[i].labelX - width(renderer, Size::S22, label) / 2, LIFTS[i].labelBase, label);
  }
  const Lift& b = LIFTS[READING];
  if (hasBook) {
    text(renderer, Size::S30, 95 + b.dx, 370 + b.dy, fit(renderer, Size::S30, book.title, 150).c_str());
    text(renderer, Size::S22, 105 + b.dx, 402 + b.dy, fit(renderer, Size::S22, book.author, 140).c_str());
    char pct[8];
    snprintf(pct, sizeof(pct), "%d%%", percent);
    text(renderer, Size::S30, 295 + b.dx, 370 + b.dy, pct);
  }
  const uint32_t today = ReadingStatsStore::currentDay();
  if (today != 0) {
    const Lift& c = LIFTS[STATS];
    char month[24], day[8];
    snprintf(month, sizeof(month), tr(STR_UGLY_DESK_MONTH), static_cast<int>((today / 100) % 100));
    snprintf(day, sizeof(day), "%d", static_cast<int>(today % 100));
    text(renderer, Size::S22, 382 + c.dx, 122 + c.dy, month);
    text(renderer, Size::S52, 410 + c.dx - (today % 100 >= 10 ? 6 : 0), 208 + c.dy, day);
  }
  topBar(renderer, nullptr);
  navRow(renderer, nullptr, tr(STR_UGLY_X4_BACK_DIARY), nullptr);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
#ifdef UGLY_FRAME_LOG
  LOG_INF("UGLY", "Desk frame total=%lums", static_cast<unsigned long>(millis() - started));
#endif
}
#else
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
#endif

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
#if FREEINK_DEVICE_X4PRO
  if (key == Key::SwipeDown) return onKey(Key::Back);  // no list to scroll here: down a tier, to the diary
  if (key == Key::Tap) {
    if (touch::notebookAt(touchX, touchY).spot == touch::Spot::Back) return onKey(Key::Back);
    const int at = touch::deskAt(touchX, touchY);
    if (at < 0) return false;
    selected = at;
    open();
    return false;
  }
  if (key != Key::Back) return false;  // no ring on the touch desk: a key that walks it would only redraw the same frame
#endif
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
