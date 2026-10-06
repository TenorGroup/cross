#include "UglyChrome.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <string>

#include "UglyInk.h"

namespace fui = freeink::ui;

namespace uglychrome {
namespace {
constexpr int LINE = 26;  // a line of the S22 pen
constexpr int MARK = 28;  // room of one mark at the row's end
}  // namespace

fui::Rect words(const GfxRenderer& r, const fui::Rect& box, const char* text, const fui::TextAlign align,
                const bool locked, const int maxLines) {
  if (!text || !*text || box.empty()) return {};
  const int asc = ugly::ascent(ugly::Size::S22);
  fui::Rect rect = box;
  if (align == fui::TextAlign::Right) {
    // The hand writes wider than the layout's font: a value at the right end grows to the left, at most twice
    // its box, before the scrawl cuts it.
    const int grow = std::min<int>(rect.width, ugly::width(r, ugly::Size::S22, text) - rect.width);
    if (grow > 0) rect = {static_cast<int16_t>(rect.x - grow), rect.y, static_cast<int16_t>(rect.width + grow), rect.height};
  }
  const auto clip = r.getClipRect();
  const int left = std::max<int>(rect.x, clip[0]), top = std::max<int>(rect.y - 4, clip[1]);
  r.setClipRect(left, top, std::max(0, std::min<int>(rect.right(), clip[0] + clip[2]) - left),
                std::max(0, std::min<int>(rect.bottom() + 6, clip[1] + clip[3]) - top));
  fui::Rect ink = rect;
  if (maxLines > 1 && rect.height >= 2 * LINE && ugly::width(r, ugly::Size::S22, text) > rect.width) {
    // Words the layout let wrap, in a box of more lines: wrapped from its top, as many lines as it holds.
    ugly::paragraph(r, ugly::Size::S22, rect.x, rect.y + asc, rect.width, LINE, text);
  } else {
    const auto line = ugly::fit(r, ugly::Size::S22, text, rect.width);
    const int w = ugly::width(r, ugly::Size::S22, line.c_str());
    const int x = align == fui::TextAlign::Center ? rect.x + (rect.width - w) / 2
                  : align == fui::TextAlign::Right ? rect.right() - w
                                                   : rect.x;
    const int base = rect.y + (rect.height + asc) / 2;
    ugly::text(r, ugly::Size::S22, x, base, line.c_str());
    if (locked) ugly::line(r, x - 2, base - asc / 3, x + w + 2, base - asc / 3 - 2, 840, 2);
    ink = {static_cast<int16_t>(x), static_cast<int16_t>(base - asc), static_cast<int16_t>(w), static_cast<int16_t>(asc + 6)};
  }
  r.setClipRect(clip[0], clip[1], clip[2], clip[3]);
  return ink;
}

void apart(const GfxRenderer& r, fui::Rect& label, const char* labelText, fui::Rect& value, const char* valueText) {
  constexpr int GAP = 14;  // the least air between the label and the value on one line
  const int span = value.right() - label.x;
  if (label.height < 2 * LINE || ugly::width(r, ugly::Size::S22, labelText) + ugly::width(r, ugly::Size::S22, valueText) + GAP <= span)
    return;
  const int16_t half = static_cast<int16_t>(label.height / 2);
  value = {label.x, static_cast<int16_t>(label.y + half), static_cast<int16_t>(span), static_cast<int16_t>(label.height - half)};
  label.width = static_cast<int16_t>(span);
  label.height = half;
}

void ring(const GfxRenderer& r, const fui::Rect& w) {
  if (!w.empty()) ugly::circle(r, ugly::Circle::Row, {w.x, w.y, w.right(), w.bottom()}, 12, 8, 2);
}

int marksWidth(const GfxRenderer& r, const Marks& m) {
  int w = (m.chosen ? MARK + 2 : 0) + (m.opensNext ? MARK : 0);
  if (m.toggle) w += ugly::width(r, ugly::Size::S22, tr(STR_STATE_OFF)) + 8;
  return w;
}

void marks(const GfxRenderer& r, const fui::Rect& box, const Marks& m) {
  const int cy = box.y + box.height / 2;
  int x = box.right() - 14;
  if (m.opensNext) {
    ugly::mark(r, ugly::Mark::Right, x, cy);
    x -= MARK;
  }
  if (m.chosen) {
    ugly::tick(r, x - 10, cy + 8);
    x -= MARK + 2;
  }
  if (m.toggle) {
    const char* state = m.toggleOn ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    ugly::text(r, ugly::Size::S22, x + 10 - ugly::width(r, ugly::Size::S22, state), cy + ugly::ascent(ugly::Size::S22) / 2, state);
  }
  if (!m.selected) return;
  if (m.around.empty())
    ugly::circle(r, ugly::Circle::Row, {box.x + 5, box.y + 5, box.right() - 5, box.bottom() - 5}, 0, 0, 2);
  else
    ring(r, m.around);
}

void row(const GfxRenderer& r, const fui::Rect& box, const Row& row) {
  if (row.heading && *row.heading) {
    const int base = box.y - 8;
    const auto name = ugly::fit(r, ugly::Size::S22, row.heading, box.width - 32);
    const int w = ugly::text(r, ugly::Size::S22, box.x + 16, base, name.c_str());
    ugly::underline(r, box.x + 14, box.x + 18 + w, base + 5, 841, 1);
  }
  auto label = box.inset(fui::Insets{0, static_cast<int16_t>(16 + marksWidth(r, row.marks)), 0, 16});
  if (row.value && *row.value) {
    const int vw = std::min<int>(label.width / 2, ugly::width(r, ugly::Size::S22, row.value));
    words(r, {static_cast<int16_t>(label.right() - vw), label.y, static_cast<int16_t>(vw), label.height}, row.value,
          fui::TextAlign::Right, row.locked);
    label.width = static_cast<int16_t>(std::max(0, label.width - vw - 12));
  }
  Marks m = row.marks;
  if (row.subtitle && *row.subtitle) {
    const int16_t half = static_cast<int16_t>(label.height / 2);
    m.around = words(r, {label.x, label.y, label.width, half}, row.label, fui::TextAlign::Left, row.locked);
    words(r, {label.x, static_cast<int16_t>(label.y + half), label.width, static_cast<int16_t>(label.height - half)},
          row.subtitle, fui::TextAlign::Left, row.locked);
  } else {
    m.around = words(r, label, row.label, fui::TextAlign::Left, row.locked);
  }
  marks(r, box, m);
}

}  // namespace uglychrome
