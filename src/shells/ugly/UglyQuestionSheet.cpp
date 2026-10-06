#include "UglyQuestionSheet.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "UglyInk.h"
#include "UglySheets.h"
#include "components/TenorMenuChrome.h"

namespace ugly {
namespace {
// The gap between 2 questions on a sheet that would otherwise leave 1 question to a sheet of its own.
constexpr int TIGHT_GAP = 2;
// Integer strokes keep the pencil marks deterministic on the C3.
uint32_t noise(uint32_t seed, int x, int y) {
  uint32_t n = seed ^ static_cast<uint32_t>(x) * 73856093u ^ static_cast<uint32_t>(y) * 19349663u;
  n ^= n >> 13;
  return n * 1274126177u;
}

void pencil(const GfxRenderer& r, int x, int y, int radius, uint32_t seed) {
  for (int dy = -radius - 2, n = 0; dy <= radius + 2; dy += 3, ++n) {
    int half = radius + 2;
    while (half > 0 && half * half + dy * dy > (radius + 3) * (radius + 3)) --half;
    const int a = x - half - dy / 2 - static_cast<int>(noise(seed, n, 0) % 3);
    const int b = x + half - dy / 2 + static_cast<int>(noise(seed, n, 1) % 4);
    line(r, a, y + dy + half / 3, b, y + dy - half / 3, seed + n, 3);
  }
  line(r, x - radius / 2, y + 3, x + radius + 7, y - radius / 2, seed + 23, 2);
}

void erasure(const GfxRenderer& r, int x, int y, int rx, int ry, uint32_t seed) {
  for (int dy = -ry; dy <= ry; dy += 4)
    for (int dx = -rx; dx <= rx; dx += 4) {
      if (dx * dx * ry * ry + dy * dy * rx * rx > rx * rx * ry * ry) continue;
      const uint32_t n = noise(seed, dx, dy);
      if (n % 5 < 3) r.fillRect(x + dx + static_cast<int>((n >> 4) % 3) - 1,
                                y + dy + static_cast<int>((n >> 8) % 3) - 1, 1 + (n % 3 == 0), 1, true);
    }
}

void dotted(const GfxRenderer& r, int x0, int x1, int y) {
  for (int x = x0; x < x1; x += 7) r.fillRect(x, y, 3, 2, true);
}

void box(const GfxRenderer& r, int x0, int y0, int x1, int y1, uint32_t seed) {
  line(r, x0, y0, x1, y0 + 1, seed);
  line(r, x1, y0 + 1, x1 + 1, y1, seed + 1);
  line(r, x1 + 1, y1, x0 - 1, y1 + 1, seed + 2);
  line(r, x0 - 1, y1 + 1, x0, y0, seed + 3);
}

void answerCircle(const GfxRenderer& r, int x, int y, int radius, int index) {
  circle(r, Circle::Word, {x - radius, y - radius, x + radius, y + radius}, 0, 0, 2);
  if (index >= 0 && index < 6) {
    char letter[2] = {static_cast<char>('A' + index), 0};
    text(r, Size::S22, x - width(r, Size::S22, letter) / 2, y + 7, letter);
  }
}

int choiceLabelWidth(int cell, bool touch) {
  const int radius = touch ? 12 : 13;
  return std::max(20, cell - 2 * radius - 12 - (touch ? 0 : 16));
}

void digits(const GfxRenderer& r, int x, int y, int available, const char* label, const char* value) {
  text(r, Size::S22, x, y - 8, label);
  const int count = static_cast<int>(strlen(value));
  const int cell = count ? std::min(18, available / count) : 18;
  for (int i = 0; i < count; ++i) {
    box(r, x + i * cell, y, x + (i + 1) * cell, y + 24, 40 + i + y);
    char digit[2] = {value[i], 0};
    text(r, Size::S22, x + i * cell + (cell - width(r, Size::S22, digit)) / 2, y + 19, digit);
  }
}
}  // namespace

QuestionSheet::Row QuestionSheet::rowAt(int index) const {
  if (!view_.row || index < 0 || index >= view_.count) return {};
  Row row = view_.row(view_.context, index);
  row.count = std::max(0, row.count);
  row.selected = std::clamp(row.selected, 0, std::max(0, row.count - 1));
  if (!row.question) row.question = "";
  return row;
}

QuestionSheet::Kind QuestionSheet::kindAt(int index) const { return blocks_[index].kind; }

void QuestionSheet::labelAt(int row, int option, char* out, size_t size) const {
  if (!size) return;
  out[0] = 0;
  if (view_.label) view_.label(view_.context, row, option, out, size);
  out[size - 1] = 0;
}

void QuestionSheet::invalidate() {
  view_ = {};
  ready_ = false;
  paper_ = false;
  answering_ = false;
}

void QuestionSheet::bind(const GfxRenderer& r, View view, bool touch) {
  const uint32_t focusId = current_ >= 0 && current_ < static_cast<int>(blocks_.size()) ? blocks_[current_].id : 0;
  const auto previous = blocks_;
  const bool wasAnswering = answering_ || paper_;
  view_ = view;
  view_.count = std::max(0, view_.count);
  touch_ = touch;
  ready_ = false;
  paper_ = false;
  answering_ = false;
  width_ = r.getScreenWidth();
  height_ = r.getScreenHeight();
  left_ = touch ? 24 : 32;
  right_ = width_ - (touch ? 120 : 128);
  margin_ = right_ + (touch ? 8 : 6);
  questionStep_ = touch ? 44 : 38;
  answerStep_ = touch ? 64 : 44;
  gap_ = touch ? 12 : 10;
  firstTop_ = touch ? 156 : 112;
  laterTop_ = touch ? 96 : 52;
  if (height_ < width_) firstTop_ = laterTop_;
  bottom_ = height_ - (touch ? 88 : 76);
  blocks_.resize(view_.count);
  int page = 0, top = firstTop_;
  current_ = 0;
  char question[384], option[192];
  for (int i = 0; i < view_.count; ++i) {
    const Row row = rowAt(i);
    if (focusId && row.id == focusId) current_ = i;
    // Buttons answer a switch like any question: its 2 answers are circles the circle walks.
    Kind kind = row.kind == Kind::Toggle && !touch_ ? Kind::Choice : row.kind;
    char prefix[32];
    snprintf(prefix, sizeof(prefix), tr(STR_UGLY_PHIEU_Q), i + 1);
    snprintf(question, sizeof(question), "%s %s%s", prefix, row.question,
             row.kind == Kind::Action || row.kind == Kind::ReadOnly ? "" : "?");
    int reserve = 0;
    if (kind == Kind::Toggle) reserve = 40;
    if (row.kind == Kind::Action) reserve = width(r, Size::S22, tr(STR_UGLY_PHIEU_ATTACHED)) + 12;
    const int qWidth = std::max(80, right_ - left_ - reserve);
    const int qLines = std::max(1, paragraph(r, Size::S30, left_, 0, qWidth, questionStep_, question, false));
    int columns = 0, answerRows = 0;
    if (kind == Kind::Choice && row.count) {
      for (const int cols : {4, 2, 1}) {
        const int available = choiceLabelWidth((right_ - left_) / cols, touch_);
        bool fits = true;
        for (int k = 0; k < row.count; ++k) {
          labelAt(i, k, option, sizeof(option));
          if (width(r, Size::S22, option) > available) { fits = false; break; }
        }
        if (fits || cols == 1) { columns = cols; break; }
      }
      answerRows = (row.count + columns - 1) / columns;
    } else if (kind == Kind::Ruler || kind == Kind::Paper || kind == Kind::ReadOnly) {
      answerRows = 1;
    }
    const int qHeight = qLines * questionStep_;
    if (qHeight + answerRows * answerStep_ > bottom_ - laterTop_ && kind == Kind::Choice) {
      kind = Kind::Paper;
      columns = 0;
      answerRows = 1;
    }
    const int blockHeight = std::max(touch ? 64 : 0, qHeight + answerRows * answerStep_) + gap_;
    int old = -1;
    for (const auto& b : previous) if (b.id == row.id) { old = b.previous; break; }
    blocks_[i] = {row.id, 0, static_cast<int16_t>(blockHeight), static_cast<int16_t>(qHeight), 0,
                  static_cast<int16_t>(columns), static_cast<int16_t>(old), kind};
  }
  // The sheets: the shared rule, with no sheet of 1 question while the questions can go otherwise.
  std::vector<int> ink(view_.count), sheetOf(view_.count);
  std::vector<uint8_t> tight(view_.count + 1, 0);
  for (int i = 0; i < view_.count; ++i) ink[i] = blocks_[i].height - gap_;
  // Touch: a choice too tall to share a sheet with the question before or after it would stand alone (the
  // founder's rule: no sheet of 1 question, on the X4 Pro too); its answers go on paper, one row on the sheet.
  for (int i = 0; touch_ && view_.count > 1 && i < view_.count; ++i) {
    if (blocks_[i].kind != Kind::Choice) continue;
    constexpr int NONE = 1 << 20;  // no neighbour on that side
    const int prev = i > 0 ? ink[i - 1] : NONE, next = i + 1 < view_.count ? ink[i + 1] : NONE;
    if (ink[i] + TIGHT_GAP + std::min(prev, next) <= bottom_ - laterTop_) continue;
    blocks_[i].kind = Kind::Paper;
    blocks_[i].columns = 0;
    ink[i] = std::max(64, blocks_[i].questionHeight + answerStep_);
    blocks_[i].height = static_cast<int16_t>(ink[i] + gap_);
  }
  pages_ = logic::layoutSheets(ink.data(), view_.count, bottom_ - firstTop_, bottom_ - laterTop_, gap_, TIGHT_GAP,
                               sheetOf.data(), tight.data());
  for (int i = 0; i < view_.count; ++i) {
    if (i == 0 || sheetOf[i] != sheetOf[i - 1]) top = sheetOf[i] ? laterTop_ : firstTop_;
    page = sheetOf[i];
    blocks_[i].top = static_cast<int16_t>(top);
    blocks_[i].page = static_cast<int16_t>(page);
    top += ink[i] + (tight[page] ? TIGHT_GAP : gap_);
  }
  if (!view_.count) pages_ = 1;
  if (view_.count) {
    page_ = blocks_[current_].page;
    candidate_ = rowAt(current_).selected;
    // A commit that rebinds (a new text size) keeps the question it was answered in open.
    if (wasAnswering && !touch_ && focusId && blocks_[current_].id == focusId) {
      answering_ = true;
      if (blocks_[current_].kind == Kind::Paper) openPaper();
    }
  } else {
    page_ = current_ = candidate_ = 0;
  }
}

QuestionSheet::Intent QuestionSheet::intent(IntentKind kind, bool repaint) const {
  const Row row = rowAt(current_);
  return {kind, row.id, view_.count ? current_ : -1, candidate_, repaint};
}

QuestionSheet::Intent QuestionSheet::focus(int row) {
  if (view_.count <= 0) return {};
  current_ = (row + view_.count) % view_.count;
  page_ = blocks_[current_].page;
  candidate_ = rowAt(current_).selected;
  return intent(IntentKind::Preview, true);
}

int QuestionSheet::answerTop(int row) const { return blocks_[row].top + blocks_[row].questionHeight; }

void QuestionSheet::openPaper() {
  const Row row = rowAt(current_);
  if (row.count <= 0) return;
  paper_ = true;
  paperAnchor_ = row.selected;
  paperAnchorTop_ = answerTop(current_);
  const int minTop = page_ ? laterTop_ : firstTop_;
  const int head = 64, tail = 44;
  const int above = std::max(0, (paperAnchorTop_ - minTop - head) / answerStep_);
  const int below = std::max(0, (bottom_ - paperAnchorTop_ - answerStep_ - tail) / answerStep_);
  paperFirst_ = std::max(0, paperAnchor_ - above);
  paperLast_ = std::min(row.count - 1, paperAnchor_ + below);
  paperTop_ = std::max(minTop, paperAnchorTop_ + (paperFirst_ - paperAnchor_) * answerStep_ - head);
  paperBottom_ = std::min(bottom_, paperAnchorTop_ + (paperLast_ - paperAnchor_ + 1) * answerStep_ + tail);
}

QuestionSheet::Intent QuestionSheet::turnSheet(int direction) {
  if (paper_ || !view_.row || !view_.count || pages_ <= 1) return {};
  const int next = (page_ + (direction < 0 ? -1 : 1) + pages_) % pages_;
  for (int i = 0; i < view_.count; ++i) if (blocks_[i].page == next) return focus(i);
  return {};
}

QuestionSheet::Intent QuestionSheet::input(Key key) {
  if (key == Key::Home) { paper_ = answering_ = false; return intent(IntentKind::Back, true); }
  if (key == Key::Back) {
    if (paper_ || answering_) {
      paper_ = answering_ = false;
      candidate_ = rowAt(current_).selected;
      return intent(IntentKind::None, true);
    }
    return intent(IntentKind::Back, true);
  }
  if (!view_.row || !view_.count) return {};
  const Row row = rowAt(current_);
  const Kind kind = kindAt(current_);
  // Buttons: front up/down walk the questions and the edge keys turn the sheet until Select enters a
  // question; inside it every up/down key walks the circle over its answers. Touch keeps its keys.
  const bool inside = paper_ || answering_;
  const bool option = key == Key::PreviousOption || key == Key::NextOption;
  if (key == Key::PreviousSheet || key == Key::NextSheet || (!touch_ && !inside && option)) {
    if (inside) return {};
    return turnSheet(key == Key::PreviousSheet || key == Key::PreviousOption ? -1 : 1);
  }
  if (key == Key::PreviousQuestion || key == Key::NextQuestion || (!touch_ && inside && option)) {
    const int step = key == Key::PreviousQuestion || key == Key::PreviousOption ? -1 : 1;
    if (!inside) return focus(current_ + step);
    if (!paper_) {
      if ((kind != Kind::Choice && kind != Kind::Ruler) || row.count <= 1) return {};
      candidate_ = (candidate_ + step + row.count) % row.count;
      return intent(IntentKind::Preview, true);
    }
    candidate_ = (candidate_ + step + row.count) % row.count;
    if (candidate_ < paperFirst_ || candidate_ > paperLast_) {
      // Re-anchor a page of the paper without moving the selected-value anchor
      // until the candidate leaves its currently visible portion.
      const int span = std::max(1, paperLast_ - paperFirst_ + 1);
      paperFirst_ = std::clamp(candidate_ - (step < 0 ? span - 1 : 0), 0, std::max(0, row.count - span));
      paperLast_ = std::min(row.count - 1, paperFirst_ + span - 1);
      paperAnchor_ = paperFirst_;
      paperAnchorTop_ = paperTop_ + 64;
    }
    return intent(IntentKind::Preview, true);
  }
  if (option) {
    if (paper_ || (kind != Kind::Choice && kind != Kind::Ruler) || row.count <= 1) return {};
    candidate_ = (candidate_ + (key == Key::PreviousOption ? -1 : 1) + row.count) % row.count;
    return intent(IntentKind::Preview, true);
  }
  if (key != Key::Confirm) return {};
  if (kind == Kind::Action) return intent(IntentKind::Activate, true);
  if (kind == Kind::ReadOnly || row.count <= 0) return {};
  if (!touch_) {
    if (!inside) {
      // Select enters the question: the circle starts on the answer in use.
      candidate_ = row.selected;
      if (kind == Kind::Paper) {
        openPaper();
        return intent(IntentKind::Preview, true);
      }
      answering_ = true;
      return intent(IntentKind::None, true);
    }
    // Another answer is chosen and the question stays open; the answer in use closes it.
    if (candidate_ != row.selected) return intent(IntentKind::Commit, true);
    paper_ = answering_ = false;
    return intent(IntentKind::None, true);
  }
  if (kind == Kind::Paper && !paper_) {
    openPaper();
    return intent(IntentKind::Preview, true);
  }
  if (kind == Kind::Toggle) candidate_ = row.selected ? 0 : 1;
  paper_ = false;
  return intent(candidate_ == row.selected ? IntentKind::None : IntentKind::Commit, true);
}

int QuestionSheet::optionAt(int index, int x, int y) const {
  const Row row = rowAt(index);
  const Block& b = blocks_[index];
  const int ay = answerTop(index);
  if (b.kind == Kind::Choice && b.columns > 0 && x >= left_ && x < right_ && y >= ay) {
    const int col = (x - left_) * b.columns / (right_ - left_);
    const int option = (y - ay) / answerStep_ * b.columns + col;
    if (option < row.count) return option;
  }
  if (b.kind == Kind::Ruler && row.count && y >= ay && y < ay + answerStep_) {
    const int a = left_ + 26, z = right_ - 14;
    return std::clamp(((x - a) * (row.count - 1) + (z - a) / 2) / std::max(1, z - a), 0, row.count - 1);
  }
  return -1;
}

QuestionSheet::Intent QuestionSheet::tap(int x, int y) {
  if (!touch_ || !ready_ || !view_.row || x < 0 || y < 0) return {};
  if (paper_) {
    if (x < 24 || x >= margin_ || y < paperTop_ || y >= paperBottom_) {
      paper_ = false;
      candidate_ = rowAt(current_).selected;
      return intent(IntentKind::None, true);
    }
    const Row row = rowAt(current_);
    if (y < paperTop_ + 34) return {};  // the paper's question is inert
    for (int k = paperFirst_; k <= paperLast_; ++k) {
      const int top = paperAnchorTop_ + (k - paperAnchor_) * answerStep_;
      if (y >= top && y < top + answerStep_) {
        candidate_ = k;
        paper_ = false;
        return intent(k == row.selected ? IntentKind::None : IntentKind::Commit, true);
      }
    }
    if ((y < paperAnchorTop_ + (paperFirst_ - paperAnchor_) * answerStep_ && paperFirst_ > 0) ||
        (y >= paperAnchorTop_ + (paperLast_ - paperAnchor_ + 1) * answerStep_ && paperLast_ + 1 < row.count)) {
      const int step = y < paperAnchorTop_ ? -1 : 1;
      const int span = std::max(1, paperLast_ - paperFirst_ + 1);
      paperFirst_ = std::clamp(paperFirst_ + step * span, 0, std::max(0, row.count - span));
      paperLast_ = std::min(row.count - 1, paperFirst_ + span - 1);
      paperAnchor_ = paperFirst_;
      paperAnchorTop_ = paperTop_ + 64;
      candidate_ = paperFirst_;
      return intent(IntentKind::Preview, true);
    }
    return {};
  }
  if (y >= height_ - 64) {
    if (x > width_ * 2 / 3) return turnSheet(1);
    if (x >= width_ / 3) return intent(IntentKind::Back, true);
    return {};
  }
  if (x >= margin_) return {};
  for (int i = 0; i < view_.count; ++i) {
    const Block& b = blocks_[i];
    if (b.page != page_ || y < b.top || y >= b.top + b.height - gap_) continue;
    const Row row = rowAt(i);
    if (b.kind == Kind::ReadOnly) return {};
    current_ = i;
    candidate_ = row.selected;
    if (b.kind == Kind::Action) return intent(IntentKind::Activate, true);
    if (b.kind == Kind::Toggle) { candidate_ = row.selected ? 0 : 1; return intent(IntentKind::Commit, true); }
    if (b.kind == Kind::Paper) {
      if (y < answerTop(i)) return {};
      openPaper();
      return intent(IntentKind::Preview, true);
    }
    const int option = optionAt(i, x, y);
    if (option < 0) return {};
    candidate_ = option;
    return intent(option == row.selected ? IntentKind::None : IntentKind::Commit, true);
  }
  return {};
}

int QuestionSheet::questionAt(int x, int y) const {
  if (!ready_ || paper_ || !view_.row || x < 0 || x >= margin_ || y < 0) return -1;
  for (int i = 0; i < view_.count; ++i) {
    const Block& b = blocks_[i];
    if (b.page == page_ && y >= b.top && y < b.top + b.height - gap_) return i;
  }
  return -1;
}

void QuestionSheet::didCommit(int row, int previousIndex) {
  if (row < 0 || row >= view_.count) return;
  blocks_[row].previous = static_cast<int16_t>(previousIndex);
  if (row == current_) candidate_ = rowAt(row).selected;
}

void QuestionSheet::setQuip(int row, std::string line) {
  quipRow_ = row;
  quip_ = std::move(line);
}

void QuestionSheet::paint(const GfxRenderer& r, const MappedInputManager& input) const {
  if (!view_.row) return;
  r.clearScreen();
  const int dy = touch_ ? 44 : 0;
  const int commentX = margin_ + 8;
  char buf[384], label[192], oldLabel[192];
  if (page_ == 0 && firstTop_ != laterTop_) {
    text(r, Size::S38, left_, 46 + dy, tr(STR_UGLY_PHIEU_TITLE));
    const int sub = text(r, Size::S22, left_, 90 + dy, tr(STR_UGLY_PHIEU_SUBJECT));
    const char* subject = view_.subject ? view_.subject : "";
    const int subjectBudget = (margin_ - 8) - (left_ + sub + 10);
    const auto subjectSize = width(r, Size::S38, subject) <= subjectBudget ? Size::S38 : Size::S30;
    const auto caption = fit(r, subjectSize, subject, subjectBudget);
    text(r, subjectSize, left_ + sub + 10, 90 + dy, caption.c_str());
    dotted(r, left_ + sub + 8, margin_ - 8, 96 + dy);
    if (view_.date) snprintf(buf, sizeof(buf), "%02u%02u%02u", static_cast<unsigned>(view_.date % 100),
                             static_cast<unsigned>(view_.date / 100 % 100), static_cast<unsigned>(view_.date / 10000 % 100));
    else snprintf(buf, sizeof(buf), "      ");
    digits(r, commentX, 26 + dy, width_ - 8 - commentX, tr(STR_UGLY_PHIEU_SBD), buf);
    snprintf(buf, sizeof(buf), "%03d", view_.code);
    digits(r, commentX, 72 + dy, width_ - 8 - commentX, tr(STR_UGLY_PHIEU_CODE), buf);
  } else {
    snprintf(buf, sizeof(buf), tr(STR_UGLY_PHIEU_HEAD2), view_.subject ? view_.subject : "");
    text(r, Size::S22, left_, 30 + dy, buf);
  }
  const int headerY = page_ ? laterTop_ - 10 : firstTop_ - 8;
  line(r, 12, headerY, width_ - 12, headerY - 1, 61, 2);
  line(r, margin_ + 1, headerY + 4, margin_ + 2, bottom_, 62);
  for (int i = 0; i < view_.count; ++i) {
    const Block& b = blocks_[i];
    if (b.page != page_) continue;
    const Row row = rowAt(i);
    char prefix[32];
    snprintf(prefix, sizeof(prefix), tr(STR_UGLY_PHIEU_Q), i + 1);
    snprintf(buf, sizeof(buf), "%s %s%s", prefix, row.question,
             row.kind == Kind::Action || row.kind == Kind::ReadOnly ? "" : "?");
    const int reserve = b.kind == Kind::Toggle ? 40 : b.kind == Kind::Action ? width(r, Size::S22, tr(STR_UGLY_PHIEU_ATTACHED)) + 12 : 0;
    const int qWidth = std::max(80, right_ - left_ - reserve);
    paragraph(r, Size::S30, left_, b.top + questionStep_ - 10, qWidth, questionStep_, buf);
    const int ay = answerTop(i), qBase = ay - 10;
    Box focusBox{left_, qBase - 28, right_, qBase + 6};
    if (b.kind == Kind::Toggle) {
      const int cy = qBase - 10;
      box(r, right_ - 32, cy - 13, right_ - 6, cy + 13, 900 + i);
      if (row.selected) {
        line(r, right_ - 30, cy - 11, right_ - 7, cy + 12, 920 + i, 3);
        line(r, right_ - 29, cy + 11, right_ - 6, cy - 12, 930 + i, 3);
      } else if (b.previous == 1) erasure(r, right_ - 19, cy, 14, 13, 960 + i);
      focusBox = {right_ - 36, cy - 18, right_, cy + 16};
    } else if (b.kind == Kind::Action) {
      const char* attached = tr(STR_UGLY_PHIEU_ATTACHED);
      const int w = width(r, Size::S22, attached);
      text(r, Size::S22, right_ - w, qBase, attached);
      focusBox = {right_ - w, qBase - 20, right_, qBase + 5};
    } else if (b.kind == Kind::Choice && b.columns > 0) {
      const int cell = (right_ - left_) / b.columns, radius = touch_ ? 12 : 13;
      for (int k = 0; k < row.count; ++k) {
        const int col = left_ + (k % b.columns) * cell + (touch_ ? 0 : 16);
        const int cx = col + radius + 2, cy = ay + (k / b.columns) * answerStep_ + answerStep_ / 2;
        answerCircle(r, cx, cy, radius, k);
        labelAt(i, k, label, sizeof(label));
        const std::string shown = fit(r, Size::S22, label, choiceLabelWidth(cell, touch_));
        text(r, Size::S22, cx + radius + 10, cy + 8, shown.c_str());
        if (k == row.selected) pencil(r, cx, cy, radius, 800 + i * 10 + k);
        else if (k == b.previous) erasure(r, cx, cy, radius + 3, radius + 3, 850 + i * 10 + k);
        if (i == current_ && k == candidate_)
          focusBox = {cx - radius - 2, cy - radius - 4, cx + radius + 10 + width(r, Size::S22, shown.c_str()), cy + radius + 2};
      }
    } else if (b.kind == Kind::Ruler && row.count > 0) {
      const int x0 = left_ + 26, x1 = right_ - 14, yr = ay + 14;
      box(r, x0 - 8, yr - 1, x1 + 8, yr + 27, 500 + i);
      for (int k = 0; k < row.count; ++k) {
        const int x = x0 + (x1 - x0) * k / std::max(1, row.count - 1);
        line(r, x, yr, x, yr + (k % 2 ? 7 : 11), 520 + k);
        labelAt(i, k, label, sizeof(label));
        // Repeated numeric units add no information to every tick. The active
        // value remains labelled even when a dense ruler omits nearby labels.
        if (char* unit = strstr(label, " pt")) *unit = 0;
        const int w = width(r, Size::S22, label);
        const int spacing = (x1 - x0) / std::max(1, row.count - 1);
        const int stride = std::max(1, (w + 4 + spacing - 1) / std::max(1, spacing));
        const bool nearActive = std::abs(k - row.selected) * spacing < w + 4 ||
                                (i == current_ && std::abs(k - candidate_) * spacing < w + 4);
        if (k == row.selected || (i == current_ && k == candidate_) || (k % stride == 0 && !nearActive))
          text(r, Size::S22, x - w / 2, yr + 26, label);
        if (k == b.previous) erasure(r, x, yr + 2, 6, 16, 560 + i);
        if (i == current_ && k == candidate_) focusBox = {x - w / 2, yr + 6, x + w / 2, yr + 29};
      }
      const int x = x0 + (x1 - x0) * row.selected / std::max(1, row.count - 1);
      line(r, x - 1, yr - 16, x + 1, yr + 12, 580 + i, 3);
      line(r, x - 6, yr - 18, x, yr - 10, 590 + i, 2);
      line(r, x, yr - 10, x + 7, yr - 19, 591 + i, 2);
    } else if (b.kind == Kind::Paper || b.kind == Kind::ReadOnly) {
      const int baseline = ay + answerStep_ - 12;
      const int lw = text(r, Size::S22, left_ + 16, baseline, tr(STR_UGLY_PHIEU_ANSWER));
      int x = left_ + 16 + lw + 18;
      dotted(r, x - 8, right_, baseline + 6);
      if (b.previous >= 0 && b.previous < row.count && b.previous != row.selected) {
        labelAt(i, b.previous, oldLabel, sizeof(oldLabel));
        const int ow = text(r, Size::S30, x, baseline, oldLabel);
        line(r, x - 3, baseline - 9, x + ow + 4, baseline - 14, 600 + i, 2);
        line(r, x - 2, baseline - 4, x + ow + 3, baseline - 9, 601 + i, 2);
        x += ow + 16;
      }
      labelAt(i, row.selected, label, sizeof(label));
      const std::string shown = fit(r, Size::S30, label, std::max(20, right_ - x));
      const int w = text(r, Size::S30, x, baseline, shown.c_str());
      focusBox = {x, baseline - 28, x + w, baseline + 5};
    }
    if (!touch_ && i == current_ && !paper_) {
      if (answering_) {
        circle(r, Circle::Row, focusBox, 10, 7, 2);
      } else {
        // Not yet entered: the whole question is underlined, line by line; the last line is cut to the text left.
        const int lines = std::max(1, b.questionHeight / questionStep_);
        int rest = width(r, Size::S30, buf);
        for (int l = 0; l < lines; ++l, rest -= qWidth)
          underline(r, left_ - 2, left_ + std::clamp(l + 1 < lines ? qWidth : rest, 40, qWidth),
                    b.top + questionStep_ - 3 + l * questionStep_, 940 + i * 8 + l, 2);
      }
      const int y = b.top + questionStep_ - 22;
      line(r, 6, y, 23, y - 1, 970, 2);
      line(r, 15, y - 6, 23, y - 1, 971, 2);
      line(r, 23, y - 1, 15, y + 5, 972, 2);
    }
  }
  if (paper_) {
    // Rub complete intersected question blocks, including their letters at an edge.
    int clearTop = paperTop_ - 12, clearBottom = paperBottom_ + 12;
    for (const auto& b : blocks_) if (b.page == page_) {
      if (b.top < clearTop && b.top + b.height > clearTop) clearTop = b.top - 6;
      if (b.top < clearBottom && b.top + b.height > clearBottom) clearBottom = b.top + b.height;
    }
    r.fillRect(8, std::max(headerY + 4, clearTop), margin_ - 10,
               std::min(bottom_ + 4, clearBottom) - std::max(headerY + 4, clearTop), false);
    box(r, 24, paperTop_, margin_ - 12, paperBottom_, 750);
    const Row row = rowAt(current_);
    const std::string head = fit(r, Size::S22, row.question, margin_ - 60);
    text(r, Size::S22, 44, paperTop_ + 28, head.c_str());
    for (int k = paperFirst_; k <= paperLast_; ++k) {
      const int cy = paperAnchorTop_ + (k - paperAnchor_) * answerStep_ + answerStep_ / 2;
      answerCircle(r, 70, cy, 11, -1);
      if (k == row.selected) pencil(r, 70, cy, 11, 790 + k);
      labelAt(current_, k, label, sizeof(label));
      const std::string shown = fit(r, Size::S30, label, margin_ - 112);
      const int w = text(r, Size::S30, 94, cy + 10, shown.c_str());
      if (!touch_ && k == candidate_) circle(r, Circle::Row, {94, cy - 16, 94 + w, cy + 14}, 12, 8, 2);
    }
    if (paperFirst_) {
      snprintf(buf, sizeof(buf), tr(STR_UGLY_MORE), paperFirst_);
      text(r, Size::S22, 44, paperTop_ + 54, buf);
      for (int x = 24; x < margin_ - 20; x += 14) line(r, x, paperTop_ - 4, x + 7, paperTop_ + 4, x, 2);
    }
    if (paperLast_ + 1 < row.count) {
      snprintf(buf, sizeof(buf), tr(STR_UGLY_MORE), row.count - paperLast_ - 1);
      text(r, Size::S22, 44, paperBottom_ - 14, buf);
      for (int x = 24; x < margin_ - 20; x += 14) line(r, x, paperBottom_ - 4, x + 7, paperBottom_ + 4, x + 1, 2);
    }
  }
  if (!quip_.empty() && quipRow_ >= 0 && quipRow_ < view_.count && blocks_[quipRow_].page == page_) {
    const int available = std::max(40, width_ - commentX - 8);
    const int lines = paragraph(r, Size::S22, commentX, 0, available, 26, quip_.c_str(), false);
    const int y = std::max(headerY + 24, std::min<int>(blocks_[quipRow_].top + 24, bottom_ - 4 - (lines - 1) * 26));
    paragraph(r, Size::S22, commentX, y, available, 26, quip_.c_str());
    underline(r, commentX, width_ - 10, y + (lines - 1) * 26 + 8, 31, 2);
  }
  snprintf(buf, sizeof(buf), tr(STR_UGLY_PHIEU_SHEET), page_ + 1, pages_);
  if (touch_) {
#if FREEINK_DEVICE_X4PRO
    formTopBar(r);
    tenorchrome::noteHandDrawn();
    char back[128];
    snprintf(back, sizeof(back), tr(STR_UGLY_X4_BACK_PARENT), tr(STR_SETTINGS_TITLE));
    const int backWidth = width_ / 3 - 12;
    const char* backText = width(r, Size::S22, back) <= backWidth ? back : tr(STR_SETTINGS_TITLE);
    const std::string backLabel = fit(r, Size::S22, backText, backWidth);
    text(r, Size::S22, (width_ - width(r, Size::S22, backLabel.c_str())) / 2, height_ - 48, backLabel.c_str());
    const int cx = width_ / 2, cy = height_ - 30;
    line(r, cx, cy - 9, cx + 1, cy + 9, 990, 2);
    line(r, cx - 7, cy + 1, cx + 1, cy + 9, 991, 2);
    line(r, cx + 1, cy + 9, cx + 8, cy, 992, 2);
    const std::string pageLabel = fit(r, Size::S22, buf, width_ / 3 - 16);
    text(r, Size::S22, width_ - 24 - width(r, Size::S22, pageLabel.c_str()), height_ - 48, pageLabel.c_str());
#endif
  } else {
    // The sheet number and what Select does now: enter the question, or choose and finish.
    snprintf(label, sizeof(label), "%s, %s", buf,
             paper_ || answering_ ? tr(STR_UGLY_PHIEU_HINT_ANSWER) : tr(STR_UGLY_PHIEU_HINT_QUESTION));
    const std::string footer = fit(r, Size::S22, label, width_ - 24);
    text(r, Size::S22, (width_ - width(r, Size::S22, footer.c_str())) / 2, height_ - 52, footer.c_str());
    statusBar(r, input, {true, view_.count > 0, true, true});
  }
  ready_ = true;
}

}  // namespace ugly
