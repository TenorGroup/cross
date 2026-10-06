#include "KeyboardEntryActivity.h"
#include "components/UIScale.h"

#include <BidiUtils.h>
#include <HalGPIO.h>
#include <I18n.h>

#include <algorithm>
#include <cstring>

#include "KeyboardLayoutSet.h"
#include "MappedInputManager.h"
#include "components/TenorMenuChrome.h"
#include "components/UITheme.h"
#include "components/themes/TenorRadius.h"
#include "fontIds.h"
#include "shells/Shell.h"
#include "shells/ugly/UglyInk.h"
#include "shells/ugly/UglySteady.h"
#include "shells/ugly/UglyWords.h"
#if defined(FREEINK_DEVICE_X4PRO)
#include "UIFontTiers.h"
#endif

namespace fui = freeink::ui;

namespace {

constexpr fui::ActionId ACTION_KEY = 1;

bool singleLineInput(const GfxRenderer& renderer) {
  return normalizedUiTextSize(SETTINGS.uiTextSize) != 0 ||
         (tenorchrome::kTouchShell && renderer.getScreenWidth() > renderer.getScreenHeight());
}

// ---------------------------------------------------------------------------
// URL layers. The SDK builtin layouts have no URL variant (":", "/", ".", the
// snippet panel), so these are app-defined tables over the same public
// KeyboardLayout structs. URLs are ASCII, so the letter rows are EN-arranged
// regardless of UI language.
// ---------------------------------------------------------------------------

#define UK(label, output, value) \
  fui::KeyboardKey { label, output, fui::KeyKind::Normal, fui::StateNormal, value, 1, true, nullptr }
#define UKA(label, output, value, alt) \
  fui::KeyboardKey { label, output, fui::KeyKind::Normal, fui::StateNormal, value, 1, true, alt }
#define UKW(label, output, value, units) \
  fui::KeyboardKey { label, output, fui::KeyKind::Normal, fui::StateNormal, value, units, true, nullptr }
#define UKS(label, kind, value, units) \
  fui::KeyboardKey { label, nullptr, kind, fui::StateNormal, value, units, true, nullptr }

constexpr int16_t URL_PANEL_VALUE = -3;  // mirrors KeyboardEntryActivity::URL_PANEL_KEY

const fui::KeyboardKey URL_NUM_ROW[] = {UKA("1", "1", '1', "!"), UKA("2", "2", '2', "@"), UKA("3", "3", '3', "#"),
                                        UKA("4", "4", '4', "$"), UKA("5", "5", '5', "%"), UKA("6", "6", '6', "^"),
                                        UKA("7", "7", '7', "&"), UKA("8", "8", '8', "*"), UKA("9", "9", '9', "("),
                                        UKA("0", "0", '0', ")")};

const fui::KeyboardKey URL_ROW1[] = {UK("q", "q", 'q'), UK("w", "w", 'w'), UK("e", "e", 'e'), UK("r", "r", 'r'),
                                     UK("t", "t", 't'), UK("y", "y", 'y'), UK("u", "u", 'u'), UK("i", "i", 'i'),
                                     UK("o", "o", 'o'), UK("p", "p", 'p')};
const fui::KeyboardKey URL_ROW2[] = {UK("a", "a", 'a'), UK("s", "s", 's'), UK("d", "d", 'd'),
                                     UK("f", "f", 'f'), UK("g", "g", 'g'), UK("h", "h", 'h'),
                                     UK("j", "j", 'j'), UK("k", "k", 'k'), UK("l", "l", 'l')};
const fui::KeyboardKey URL_ROW3[] = {UKS("Shift", fui::KeyKind::Shift, fui::QWERTY_KEY_SHIFT, 2),
                                     UK("z", "z", 'z'),
                                     UK("x", "x", 'x'),
                                     UK("c", "c", 'c'),
                                     UK("v", "v", 'v'),
                                     UK("b", "b", 'b'),
                                     UK("n", "n", 'n'),
                                     UK("m", "m", 'm'),
                                     UKS("Del", fui::KeyKind::Delete, fui::QWERTY_KEY_BACKSPACE, 2)};
// URLs have no spaces, so the URL bottom row spends the space slot on ":",
// "/", "." and the snippet-panel toggle instead (the legacy keyboard did the
// same with its "URL" key).
const fui::KeyboardKey URL_BOTTOM[] = {UKS("?123", fui::KeyKind::Mode, fui::QWERTY_KEY_MODE, 2),
                                       UK(":", ":", ':'),
                                       UK("/", "/", '/'),
                                       UK(".", ".", '.'),
                                       UKW("URL", nullptr, URL_PANEL_VALUE, 2),
                                       UKS("OK", fui::KeyKind::Ok, fui::QWERTY_KEY_ENTER, 2)};

const fui::KeyboardKey URL_SHIFT_ROW1[] = {UK("Q", "Q", 'Q'), UK("W", "W", 'W'), UK("E", "E", 'E'), UK("R", "R", 'R'),
                                           UK("T", "T", 'T'), UK("Y", "Y", 'Y'), UK("U", "U", 'U'), UK("I", "I", 'I'),
                                           UK("O", "O", 'O'), UK("P", "P", 'P')};
const fui::KeyboardKey URL_SHIFT_ROW2[] = {UK("A", "A", 'A'), UK("S", "S", 'S'), UK("D", "D", 'D'),
                                           UK("F", "F", 'F'), UK("G", "G", 'G'), UK("H", "H", 'H'),
                                           UK("J", "J", 'J'), UK("K", "K", 'K'), UK("L", "L", 'L')};
const fui::KeyboardKey URL_SHIFT_ROW3[] = {UKS("Shift", fui::KeyKind::Shift, fui::QWERTY_KEY_SHIFT, 2),
                                           UK("Z", "Z", 'Z'),
                                           UK("X", "X", 'X'),
                                           UK("C", "C", 'C'),
                                           UK("V", "V", 'V'),
                                           UK("B", "B", 'B'),
                                           UK("N", "N", 'N'),
                                           UK("M", "M", 'M'),
                                           UKS("Del", fui::KeyKind::Delete, fui::QWERTY_KEY_BACKSPACE, 2)};

// Snippet keys: multi-character outputs, stable ids above the localized-key
// range so they never collide with layout key ids.
const fui::KeyboardKey URL_SNIP_ROW1[] = {UK("https://", "https://", 2001), UK("www.", "www.", 2002),
                                          UK(".com", ".com", 2003)};
const fui::KeyboardKey URL_SNIP_ROW2[] = {UK("http://", "http://", 2004), UK("192.168.", "192.168.", 2005),
                                          UK(".org", ".org", 2006)};
const fui::KeyboardKey URL_SNIP_ROW3[] = {UK("/opds", "/opds", 2007), UK(":8080", ":8080", 2008),
                                          UK(".net", ".net", 2009)};
const fui::KeyboardKey URL_SNIP_BOTTOM[] = {UKS("abc", fui::KeyKind::Mode, fui::QWERTY_KEY_MODE, 2),
                                            UKW("URL", nullptr, URL_PANEL_VALUE, 2),
                                            UKS("Del", fui::KeyKind::Delete, fui::QWERTY_KEY_BACKSPACE, 2),
                                            UKS("OK", fui::KeyKind::Ok, fui::QWERTY_KEY_ENTER, 2)};

#undef UK
#undef UKA
#undef UKW
#undef UKS

const fui::KeyboardRow URL_ROWS[] = {
    {URL_NUM_ROW, 10, 0}, {URL_ROW1, 10, 0}, {URL_ROW2, 9, 1}, {URL_ROW3, 9, 0}, {URL_BOTTOM, 6, 0}};
const fui::KeyboardRow URL_SHIFT_ROWS[] = {
    {URL_NUM_ROW, 10, 0}, {URL_SHIFT_ROW1, 10, 0}, {URL_SHIFT_ROW2, 9, 1}, {URL_SHIFT_ROW3, 9, 0}, {URL_BOTTOM, 6, 0}};
const fui::KeyboardRow URL_SNIP_ROWS[] = {
    {URL_SNIP_ROW1, 3, 0}, {URL_SNIP_ROW2, 3, 0}, {URL_SNIP_ROW3, 3, 0}, {URL_SNIP_BOTTOM, 4, 0}};

const fui::KeyboardLayout URL_LAYOUT{URL_ROWS, 5};
const fui::KeyboardLayout URL_SHIFT_LAYOUT{URL_SHIFT_ROWS, 5};
const fui::KeyboardLayout URL_SNIPPET_LAYOUT{URL_SNIP_ROWS, 4};


// tenor/ugly on the button readers: every key written by hand in the box the FreeInkUI keyboard laid out for it (the
// keyboard registered its hits with its own painting off). The pen circles the key under the cursor.
void paintUglyKeys(const GfxRenderer& r, const fui::KeyboardProps& props, const fui::Interaction* hits, const size_t count,
                   const int rowHeight) {
  const fui::KeyboardLayout& layout = *props.layout;
  const ugly::Steady steady;  // straight letters: each key read right, and a frame per cursor move as quick as before
  for (size_t i = 0; i < count; ++i) {
    const fui::KeyboardKey* key = nullptr;
    int index = 0, logical = -1;
    for (int row = 0; row < layout.rowCount && !key; ++row)
      for (int col = 0; col < layout.rows[row].count; ++col, ++index)
        if (layout.rows[row].keys[col].value == hits[i].value) {
          key = &layout.rows[row].keys[col];
          logical = index;
          break;
        }
    if (!key) continue;
    const fui::Rect& box = hits[i].rect;
    const int cx = box.x + box.width / 2, cy = box.y + rowHeight / 2;
    const char* label = key->label;
    if (key->kind == fui::KeyKind::Ok && props.okLabel) label = props.okLabel;
    if (key->kind == fui::KeyKind::Shift && props.shiftLabel) label = props.shiftLabel;
    if (key->kind == fui::KeyKind::Mode && props.modeLabel) label = props.modeLabel;
    ugly::Box ink{cx - 10, cy - 10, cx + 10, cy + 10};
    if (key->kind == fui::KeyKind::Space) {
      ugly::underline(r, cx - box.width * 9 / 20, cx + box.width * 9 / 20, cy + 6, 700 + static_cast<uint32_t>(i), 2);
      ink = {cx - box.width * 9 / 20, cy - 4, cx + box.width * 9 / 20, cy + 10};
    } else if (key->kind == fui::KeyKind::Delete || key->kind == fui::KeyKind::Lang) {
      ugly::mark(r, key->kind == fui::KeyKind::Delete ? ugly::Mark::Left : ugly::Mark::Right, cx, cy);
    } else if (label) {
      const ugly::Size size = ugly::width(r, ugly::Size::S30, label) <= box.width - 6 ? ugly::Size::S30 : ugly::Size::S22;
      const int w = ugly::width(r, size, label), up = ugly::ascent(size);
      ugly::text(r, size, cx - w / 2, cy + up / 2, label);
      ink = {cx - w / 2, cy - up / 2, cx + w / 2, cy + up / 2};
    }
    if (key->kind == fui::KeyKind::Normal && key->alt)
      ugly::text(r, ugly::Size::S22, box.x + box.width - 2 - ugly::width(r, ugly::Size::S22, key->alt), box.y + 20, key->alt);
    if (logical == props.selectedIndex) ugly::circle(r, ugly::Circle::Word, ink, 6, 4, 2);
  }
}

}  // namespace

void KeyboardEntryActivity::onEnter() {
  Activity::onEnter();
  idleSince = static_cast<uint32_t>(millis());
  // ActivityManager publishes the activity before calling onEnter unlocked.
  // A pending render notification can already be reading its initial state.
  RenderLock lock(*this);
  cursorPos = text.length();
  // URL layers are EN-arranged app tables; everything else opens on the UI
  // language's layout, or on an enabled one if the user switched that off.
  layoutId = inputType == InputType::Url ? fui::KeyboardLayoutId::QwertyEn : keyboard_layouts::startingLayout();
  // The key only earns its slot in the bottom row with somewhere to go.
  const uint16_t enabledLayouts = keyboard_layouts::enabled();
  showLangKey = (enabledLayouts & (enabledLayouts - 1)) != 0;
  shifted = false;
  symbols = false;
  urlPanel = false;
  cursorMode = false;
  togglePos = false;
  passwordVisible = false;
  selRow = 0;
  selCol = 0;
  delPressCount = 0;
  hintVisible = false;
  hintShowTime = 0;
  rightHeld = false;
  rightLongHandled = false;
  savedCursorPos = 0;
  rightStartCursorPos = 0;
  touchRouter.reset();
  touchRouter.holdMs = TOUCH_LONG_PRESS_MS;
  touchRouter.overrideHoldMs = TOUCH_DEL_LONG_PRESS_MS;
  interactionsReady = false;
  requestUpdate();
}

void KeyboardEntryActivity::onExit() { Activity::onExit(); }

const fui::KeyboardLayout& KeyboardEntryActivity::currentLayout() const {
  if (symbols) return fui::builtinKeyboardLayout(layoutId, shifted, true);
  if (inputType == InputType::Url) {
    if (urlPanel) return URL_SNIPPET_LAYOUT;
    return shifted ? URL_SHIFT_LAYOUT : URL_LAYOUT;
  }
  return fui::builtinKeyboardLayout(layoutId, shifted, false, /*numberRow=*/true, showLangKey);
}

const fui::KeyboardKey* KeyboardEntryActivity::selectedKey() const {
  const fui::KeyboardLayout& layout = currentLayout();
  if (selRow < 0 || selRow >= layout.rowCount) return nullptr;
  const fui::KeyboardRow& row = layout.rows[selRow];
  if (selCol < 0 || selCol >= row.count) return nullptr;
  return &row.keys[selCol];
}

int KeyboardEntryActivity::selectedLogicalIndex() const {
  const fui::KeyboardLayout& layout = currentLayout();
  int index = 0;
  for (int r = 0; r < selRow && r < layout.rowCount; r++) {
    index += layout.rows[r].count;
  }
  return index + selCol;
}

void KeyboardEntryActivity::clampSelection() {
  const fui::KeyboardLayout& layout = currentLayout();
  if (layout.rowCount == 0) {
    selRow = 0;
    selCol = 0;
    return;
  }
  if (selRow < 0) selRow = 0;
  if (selRow >= layout.rowCount) selRow = layout.rowCount - 1;
  const int cols = layout.rows[selRow].count;
  if (selCol < 0) selCol = 0;
  if (selCol >= cols) selCol = cols > 0 ? cols - 1 : 0;
}

void KeyboardEntryActivity::moveSelectionRow(const int delta) {
  const fui::KeyboardLayout& layout = currentLayout();
  if (layout.rowCount == 0) return;
  if (colAnchorWidth <= 0) {
    setColumnAnchor();
  }
  selRow = (selRow + delta + layout.rowCount) % layout.rowCount;
  const int newCols = layout.rows[selRow].count;
  // Proportional mapping keeps vertical travel intuitive between rows of
  // different key counts, and it maps from the anchor so a round trip returns
  // to the key it started on. Rounds to nearest rather than truncating.
  if (colAnchorWidth > 0 && newCols > 0) {
    selCol = SETTINGS.keyboardAligned && colAnchorAligned && newCols >= 7
                 ? colAnchor
                 : (colAnchor * newCols + colAnchorWidth / 2) / colAnchorWidth;
  }
  clampSelection();
}

void KeyboardEntryActivity::moveSelectionCol(const int delta) {
  const fui::KeyboardLayout& layout = currentLayout();
  if (selRow < 0 || selRow >= layout.rowCount) return;
  const int cols = layout.rows[selRow].count;
  if (cols <= 0) return;
  selCol = (selCol + delta + cols) % cols;
  setColumnAnchor();
}

void KeyboardEntryActivity::setColumnAnchor() {
  const fui::KeyboardLayout& layout = currentLayout();
  if (selRow < 0 || selRow >= layout.rowCount) return;
  colAnchor = selCol;
  colAnchorWidth = layout.rows[selRow].count;
  colAnchorAligned = colAnchorWidth >= 7;
}

bool KeyboardEntryActivity::syncSelectionToValue(const int16_t value) {
  const fui::KeyboardLayout& layout = currentLayout();
  for (int r = 0; r < layout.rowCount; r++) {
    for (int c = 0; c < layout.rows[r].count; c++) {
      if (layout.rows[r].keys[c].value == value) {
        selRow = r;
        selCol = c;
        setColumnAnchor();
        return true;
      }
    }
  }
  return false;
}

size_t KeyboardEntryActivity::utf8Prev(const std::string& s, size_t pos) {
  if (pos == 0) return 0;
  pos--;
  while (pos > 0 && (static_cast<uint8_t>(s[pos]) & 0xC0) == 0x80) pos--;
  return pos;
}

size_t KeyboardEntryActivity::utf8Next(const std::string& s, size_t pos) {
  if (pos >= s.length()) return s.length();
  pos++;
  while (pos < s.length() && (static_cast<uint8_t>(s[pos]) & 0xC0) == 0x80) pos++;
  return pos;
}

void KeyboardEntryActivity::insertUtf8(const char* out) {
  if (!out || !*out) return;
  const size_t n = strlen(out);
  if (maxLength != 0 && text.length() + n > maxLength) return;
  if (cursorPos > text.length()) cursorPos = text.length();
  text.insert(cursorPos, out, n);
  cursorPos += n;
}

bool KeyboardEntryActivity::backspaceUtf8() {
  if (text.empty() || cursorPos == 0) return false;
  const size_t prev = utf8Prev(text, cursorPos);
  text.erase(prev, cursorPos - prev);
  cursorPos = prev;
  return true;
}

bool KeyboardEntryActivity::activateValue(const int16_t value, const bool longPress, bool& complete) {
  switch (value) {
    case fui::QWERTY_KEY_SHIFT:
      delPressCount = 0;
      hintVisible = false;
      // Letters: case toggle. Symbols: pages between "?123" and "#+=".
      shifted = !shifted;
      clampSelection();
      return true;
    case fui::QWERTY_KEY_MODE:
      delPressCount = 0;
      hintVisible = false;
      if (urlPanel) {
        urlPanel = false;
      } else {
        symbols = !symbols;
        shifted = false;
      }
      clampSelection();
      return true;
    case fui::QWERTY_KEY_LANG: {
      delPressCount = 0;
      hintVisible = false;
      const fui::KeyboardLayoutId nextId = keyboard_layouts::next(layoutId);
      // The non-Latin tables draw the key even with one layout enabled; a
      // full-screen e-ink repaint for an unchanged keyboard costs a second.
      if (nextId == layoutId) return false;
      layoutId = nextId;
      // Shift is per-layer: carrying it across would strand the new layout in
      // upper case. Row widths differ between scripts (Cyrillic runs 12/11/11
      // against Latin's 10/9/9), so the selection has to be re-clamped.
      shifted = false;
      clampSelection();
      return true;
    }
    case URL_PANEL_KEY:
      delPressCount = 0;
      hintVisible = false;
      urlPanel = !urlPanel;
      symbols = false;
      shifted = false;
      clampSelection();
      return true;
    case fui::QWERTY_KEY_ENTER:
      complete = true;
      return false;
    case fui::QWERTY_KEY_BACKSPACE:
      if (longPress) {
        text.clear();
        cursorPos = 0;
        return true;
      }
      delPressCount++;
      if (delPressCount >= 2) {
        hintVisible = true;
        hintShowTime = millis();
      }
      backspaceUtf8();
      return true;
    default: {
      delPressCount = 0;
      hintVisible = false;
      const fui::KeyboardLayout& layer = currentLayout();
      // keyboardAltOutputFor covers explicit alts and the letter case-flip.
      const char* out = longPress ? fui::keyboardAltOutputFor(layer, value) : nullptr;
      if (!out) out = fui::keyboardOutputFor(layer, value);
      if (!out) return false;
      insertUtf8(out);
      if (shifted && !symbols) {
        shifted = false;  // shift auto-releases after one character
        clampSelection();
      }
      return true;
    }
  }
}

bool KeyboardEntryActivity::clearAllOrAltOnSelected() {
  const fui::KeyboardKey* key = selectedKey();
  if (!key) return false;
  if (key->value == fui::QWERTY_KEY_BACKSPACE) {
    text.clear();
    cursorPos = 0;
    return true;
  }
  // Explicit alts and the letter case-flip, same as touch long-press.
  const char* alt = fui::keyboardAltOutputFor(currentLayout(), key->value);
  if (alt) {
    insertUtf8(alt);
    return true;
  }
  return false;
}

std::string KeyboardEntryActivity::displayTextForCurrentState() const {
  std::string displayText = text;
  if (inputType != InputType::Password || passwordVisible) {
    return displayText;
  }

  // Keep byte offsets for the cursor, but reveal a whole UTF-8 character.
  // In cursor mode the block draws the actual character separately.
  const size_t revealEnd = cursorMode ? 0 : cursorPos;
  const size_t revealStart = utf8Prev(text, revealEnd);
  for (size_t i = 0; i < displayText.length(); i++) {
    if (i < revealStart || i >= revealEnd) {
      displayText[i] = '*';
    }
  }
  return displayText;
}

int KeyboardEntryActivity::measureRange(std::string& s, const int start, const int end) const {
  if (end <= start) return 0;
  // s[end] is writable even at s.length() (the terminator slot); only '\0' may
  // be written there, which is exactly what the measurement needs.
  const char saved = s[end];
  s[end] = '\0';
  const int width = renderer.getTextAdvanceX(UI_12_FONT_ID, s.c_str() + start, EpdFontFamily::REGULAR);
  s[end] = saved;
  return width;
}

bool KeyboardEntryActivity::rangeIsRtl(std::string& s, const int start, const int end) const {
  if (end <= start) return false;
  const char saved = s[end];
  s[end] = '\0';
  const bool isRtl = BidiUtils::detectParagraphLevel(s.c_str() + start, 0, end - start) != 0;
  s[end] = saved;
  return isRtl;
}

int KeyboardEntryActivity::lineBreakEnd(std::string& s, const int start, const int maxWidth) const {
  const int len = static_cast<int>(s.length());
  if (measureRange(s, start, len) <= maxWidth) return len;
  int lo = start;
  int hi = len;
  while (true) {
    int mid = lo + (hi - lo) / 2;
    // Measure only complete code points. Truncated prefixes reach the font
    // decoder as replacement characters and give unreliable widths.
    while (mid > lo && (static_cast<uint8_t>(s[mid]) & 0xC0) == 0x80) mid--;
    if (mid == lo) mid = static_cast<int>(utf8Next(s, static_cast<size_t>(lo)));
    if (mid >= hi) break;
    if (measureRange(s, start, mid) <= maxWidth) {
      lo = mid;
    } else {
      hi = mid;
    }
  }

  // An oversized first character still occupies one line so wrapping advances.
  return lo > start ? lo : static_cast<int>(utf8Next(s, static_cast<size_t>(start)));
}

bool KeyboardEntryActivity::cursorPositionFromPoint(const int x, const int y, size_t& position) const {
  // Key taps are the overwhelmingly common case; they land on the keyboard,
  // never the text field, so skip the wrap/measure work entirely.
  if (y >= keyboardRect().y) return false;

  const int pageWidth = renderer.getScreenWidth();
  const auto& metrics = UITheme::getInstance().getMetrics();

  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int inputStartY = inputTop();

  int availableWidth = pageWidth;
  if (gpio.deviceIsX3()) {
    availableWidth -= 2 * metrics.sideButtonHintsWidth;
  }
  const int effectiveMargin = (pageWidth - availableWidth * metrics.keyboardTextFieldWidthPercent / 100) / 2;
  const int toggleGap = inputType == InputType::Password ? 4 : 0;
  const int toggleReserve = inputType == InputType::Password ? std::max(renderer.getTextWidth(UI_12_FONT_ID, "[abc]"),
                                                                        renderer.getTextWidth(UI_12_FONT_ID, "[***]")) +
                                                                   toggleGap
                                                             : 0;
  const int textAreaWidth = pageWidth - 2 * effectiveMargin - toggleReserve;
  const int maxLineWidth = textAreaWidth;
  const bool centerText = metrics.keyboardCenteredText;
  std::string displayText = displayTextForCurrentState();

  int lineStartIdx = inputWindowStart(displayText, maxLineWidth);
  int lineY = inputStartY;
  int lastLineStartIdx = 0;
  int lastLineEndIdx = static_cast<int>(displayText.length());
  int lastLineStartX = effectiveMargin;
  int lastLineWidth = 0;

  while (true) {
    const int lineEndIdx = lineBreakEnd(displayText, lineStartIdx, maxLineWidth);
    const int textWidth = measureRange(displayText, lineStartIdx, lineEndIdx);
    const int lineStartX = centerText ? effectiveMargin + (maxLineWidth - textWidth) / 2 : effectiveMargin;
    const bool isRtl = rangeIsRtl(displayText, lineStartIdx, lineEndIdx);
    lastLineStartIdx = lineStartIdx;
    lastLineEndIdx = lineEndIdx;
    lastLineStartX = lineStartX;
    lastLineWidth = textWidth;

    if (y >= lineY - metrics.verticalSpacing && y < lineY + lineHeight + metrics.verticalSpacing) {
      if (x <= lineStartX) {
        position = static_cast<size_t>(isRtl ? lineEndIdx : lineStartIdx);
        return true;
      }
      if (x >= lineStartX + textWidth) {
        position = static_cast<size_t>(isRtl ? lineStartIdx : lineEndIdx);
        return true;
      }

      int previousWidth = 0;
      for (int i = lineStartIdx; i < lineEndIdx;) {
        const int next = static_cast<int>(utf8Next(displayText, static_cast<size_t>(i)));
        const int nextWidth = measureRange(displayText, lineStartIdx, next);
        const int halfAdvance = (nextWidth - previousWidth) / 2;
        const int midpoint =
            isRtl ? lineStartX + textWidth - previousWidth - halfAdvance : lineStartX + previousWidth + halfAdvance;
        if ((isRtl && x >= midpoint) || (!isRtl && x < midpoint)) {
          position = static_cast<size_t>(i);
          return true;
        }
        previousWidth = nextWidth;
        i = next;
      }
      position = static_cast<size_t>(lineEndIdx);
      return true;
    }

    if (singleLineInput(renderer) || lineEndIdx == static_cast<int>(displayText.length())) {
      break;
    }

    lineY += lineHeight;
    lineStartIdx = lineEndIdx;
  }

  const int underlineBottom = lineY + lineHeight + metrics.verticalSpacing + 8;
  if (y >= inputStartY - metrics.verticalSpacing && y < underlineBottom && x >= effectiveMargin &&
      x < effectiveMargin + maxLineWidth + toggleReserve) {
    const bool isRtl = rangeIsRtl(displayText, lastLineStartIdx, lastLineEndIdx);
    const bool insideText = x < lastLineStartX + lastLineWidth;
    position = static_cast<size_t>(insideText == isRtl ? lastLineEndIdx : lastLineStartIdx);
    return true;
  }

  return false;
}

int KeyboardEntryActivity::inputTop() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  if (tenorchrome::kTouchShell) return tenorchrome::contentTop();
  if (normalizedUiTextSize(SETTINGS.uiTextSize) != 0)
    return metrics.topPadding + (tenorchrome::enabled() ? tenorchrome::headerHeight() : metrics.headerHeight) +
           metrics.verticalSpacing;
  return metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing * 5 + metrics.keyboardVerticalOffset;
}

int KeyboardEntryActivity::inputWindowStart(std::string& displayText, int maxWidth) const {
  if (!singleLineInput(renderer)) return 0;
  int start = 0;
  while (start < static_cast<int>(displayText.size())) {
    const int end = lineBreakEnd(displayText, start, maxWidth);
    if (end >= static_cast<int>(displayText.size()) || cursorPos < static_cast<size_t>(end)) break;
    start = end;
  }
  return start;
}

fui::Rect KeyboardEntryActivity::keyboardRect() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int rows = currentLayout().rowCount;
  const int gap = metrics.keyboardKeySpacing;
  const bool landscape = pageWidth > pageHeight;
  const int rowGap = tenorchrome::kTouchShell ? (landscape ? 0 : fui::KeyboardProps{}.rowGap) : gap;
  const bool enlarged = normalizedUiTextSize(SETTINGS.uiTextSize) != 0;
  int keyHeight = enlarged ? renderer.getLineHeight(UI_12_FONT_ID) + renderer.getLineHeight(SMALL_FONT_ID) + 8
                           : metrics.keyboardKeyHeight;
  if (tenorchrome::kTouchShell) {
    const int inputBottom = inputTop() + renderer.getLineHeight(UI_12_FONT_ID) + metrics.verticalSpacing + (landscape ? 2 : 8);
    const int bottomGap = landscape ? 4 : metrics.verticalSpacing + 8;
    const int available = tenorchrome::footBackTop(pageHeight) - bottomGap - inputBottom;
    keyHeight = std::min(landscape ? 60 : std::max(60, keyHeight),
                         std::max(1, (available - (rows - 1) * rowGap) / rows));
  }
  const int height = rows * keyHeight + (rows > 1 ? (rows - 1) * rowGap : 0);
  const int width = pageWidth * metrics.keyboardWidthPercent / 100;
  const int x = (pageWidth - width) / 2;
  int insetTop = 0, insetRight = 0, insetBottom = 0, insetLeft = 0;
  if (enlarged) renderer.getOrientedViewableTRBL(&insetTop, &insetRight, &insetBottom, &insetLeft);
  // Touch: the keys sit over the foot of the screen, where "<" keeps a place of its own.
  const int hints = tenorchrome::kTouchShell ? tenorchrome::footBackReserve() : metrics.buttonHintsHeight;
  const int footerReserve = std::max(hints, insetBottom);
  const int y = tenorchrome::kTouchShell ? tenorchrome::footBackTop(pageHeight) - height -
                                             (landscape ? 4 : metrics.verticalSpacing + 8)
                : enlarged ? pageHeight - footerReserve - height -
                               (12 + 2 * renderer.getLineHeight(SMALL_FONT_ID))
                         : pageHeight - hints - metrics.verticalSpacing - height +
                               metrics.keyboardVerticalOffset -
                               (tenorchrome::enabled() && !tenorchrome::kTouchShell
                                    ? 28 + 6 * renderer.getLineHeight(SMALL_FONT_ID)
                                    : 0);
  return fui::Rect{static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(width),
                   static_cast<int16_t>(height)};
}

void KeyboardEntryActivity::loop() {
  // GPIO state stays sampled while the main task waits for RenderLock, but
  // getHeldTime() keeps advancing. Capture input before that wait so a short
  // tap cannot become a destructive hold just because a display frame is slow.
  const auto button = [this](MappedInputManager::Button key) {
    return ButtonInput{mappedInput.wasPressed(key), mappedInput.wasReleased(key), mappedInput.isPressed(key)};
  };
  const bool swapAxis = SETTINGS.keyboardAxisSwapped != 0;
  InputFrame input;
  input.rowPrev = button(swapAxis ? MappedInputManager::Button::Left : MappedInputManager::Button::Up);
  input.rowNext = button(swapAxis ? MappedInputManager::Button::Right : MappedInputManager::Button::Down);
  input.colPrev = button(swapAxis ? MappedInputManager::Button::Up : MappedInputManager::Button::Left);
  input.colNext = button(swapAxis ? MappedInputManager::Button::Down : MappedInputManager::Button::Right);
  input.confirm = button(MappedInputManager::Button::Confirm);
  input.back = button(MappedInputManager::Button::Back);
  input.tapped = mappedInput.wasScreenTapped(input.tapX, input.tapY);
  input.touchDown = mappedInput.wasScreenTouchDown(input.touchX, input.touchY);
  int hx = 0, hy = 0;
  input.touchHeld = mappedInput.isScreenTouchHeld(hx, hy);
  input.heldMs = mappedInput.getHeldTime();
  input.capturedAt = millis();

  const auto buttonActive = [](const ButtonInput& value) { return value.pressed || value.released || value.held; };
  const bool interaction = buttonActive(input.rowPrev) || buttonActive(input.rowNext) ||
                           buttonActive(input.colPrev) || buttonActive(input.colNext) ||
                           buttonActive(input.confirm) || buttonActive(input.back) || input.tapped || input.touchDown ||
                           input.touchHeld;
  if (keyboard_power::idleTimeoutDue(static_cast<uint32_t>(input.capturedAt), idleTimeoutMs, interaction,
                                     idleSince)) {
    onTimeout();
    return;
  }

  bool complete = false;
  std::string completedText;
  {
    // render() retains this same lock through its entire frame, so its text
    // copy, cursor spans, visibility and layout all describe one input state.
    RenderLock lock(*this);
    loopLocked(input, complete);
    if (complete) completedText = text;
  }
  if (complete) {
    if (backCancels && input.back.released) onCancel();
    else onComplete(std::move(completedText));
  }
}

bool KeyboardEntryActivity::saveInputBeforeHome() {
  if (backCancels) { onCancel(); return true; }
  std::string completedText;
  {
    RenderLock lock(*this);
    completedText = text;
  }
  onComplete(std::move(completedText));
  return true;
}

void KeyboardEntryActivity::loopLocked(const InputFrame& input, bool& complete) {
  size_t touchedCursorPos = 0;
  if (input.tapped && cursorPositionFromPoint(input.tapX, input.tapY, touchedCursorPos)) {
    cursorPos = std::min(touchedCursorPos, text.length());
    // The masked text field maps taps per byte; snap back to a boundary so
    // the cursor never lands inside a multi-byte character.
    while (cursorPos > 0 && cursorPos < text.length() && (static_cast<uint8_t>(text[cursorPos]) & 0xC0) == 0x80) {
      cursorPos--;
    }
    cursorMode = false;
    togglePos = false;
    hintVisible = false;
    touchRouter.reset();
    requestUpdate();
    return;
  }

  if (!cursorMode && interactionsReady) {
    const fui::TouchHoldRouter::Result result =
        touchRouter.update(interactions, input.touchDown, static_cast<int16_t>(input.touchX),
                           static_cast<int16_t>(input.touchY), input.tapped, static_cast<int16_t>(input.tapX),
                           static_cast<int16_t>(input.tapY), input.touchHeld, input.capturedAt);
    if (result.event) {
      syncSelectionToValue(result.event.value);
      if (activateValue(result.event.value, result.event.longPress, complete)) {
        requestUpdate();
      }
      return;
    }
    if (result.activeChanged) {
      requestUpdate();
    }
    // A tap on "<" in the bar is Back (input.back), not a touch on the keys.
    if ((input.touchDown || input.tapped) && !input.back.released) {
      return;
    }
  }

  if (!cursorMode && input.rowPrev.pressed) {
    upHeld = true;
    upLongHandled = false;
  }

  if (upHeld && !upLongHandled && input.rowPrev.held && input.heldMs > LONG_PRESS_MS) {
    cursorMode = true;
    upLongHandled = true;
    hintVisible = true;
    hintShowTime = millis();
    requestUpdate();
  }

  if (input.rowPrev.released) {
    if (upHeld && !upLongHandled && !cursorMode) {
      moveSelectionRow(-1);
      requestUpdate();
    }
    upHeld = false;
    upLongHandled = false;
  }

  if (input.rowNext.pressed) {
    downHeld = true;
    if (cursorMode) {
      togglePos = false;
      passwordVisible = false;
      cursorMode = false;
      hintVisible = false;
      downLongHandled = true;
      requestUpdate();
    } else {
      downLongHandled = false;
    }
  }

  if (input.rowNext.released) {
    if (downHeld && !downLongHandled && !cursorMode) {
      moveSelectionRow(1);
      requestUpdate();
    }
    downHeld = false;
    downLongHandled = false;
  }

  // A tap steps one key on the keyboard and one character in cursor mode; a hold
  // deletes backwards in both, so a typo found while walking the cursor can be
  // fixed on the spot. The hold replaces the old continuous column repeat: one
  // gesture cannot both scan across keys and fire an edit.
  if (input.colPrev.pressed) {
    colPrevHeld = true;
    colPrevLongHandled = false;
  }

  if (colPrevHeld && !colPrevLongHandled && input.colPrev.held &&
      input.heldMs > LONG_PRESS_MS) {
    if (backspaceUtf8()) {
      requestUpdate();
    }
    colPrevLongHandled = true;
  }

  if (input.colPrev.released) {
    const bool wasHeld = colPrevHeld;
    const bool edited = colPrevLongHandled;
    colPrevHeld = false;
    colPrevLongHandled = false;
    if (wasHeld && !edited) {
      if (!cursorMode) {
        moveSelectionCol(-1);
        requestUpdate();
      } else if (togglePos) {
        cursorPos = savedCursorPos;
        togglePos = false;
        requestUpdate();
      } else if (cursorPos > 0) {
        cursorPos = utf8Prev(text, cursorPos);
        requestUpdate();
      }
    }
  }

  if (input.colNext.pressed) {
    colNextHeld = true;
    colNextLongHandled = false;
    if (cursorMode && inputType == InputType::Password && !togglePos) {
      rightHeld = true;
      rightLongHandled = false;
      rightStartCursorPos = cursorPos;
    }
  }

  // Hold inserts a space: the space bar sits two rows away from the letters, so
  // typing a multi-word name meant crossing the layout for every gap, and cursor
  // mode had no way to add one at all. A password field keeps this gesture for
  // its reveal instead.
  if (colNextHeld && !colNextLongHandled && input.colNext.held &&
      input.heldMs > LONG_PRESS_MS) {
    if (rightHeld && !rightLongHandled) {
      savedCursorPos = rightStartCursorPos;
      togglePos = true;
      rightLongHandled = true;
    } else {
      insertUtf8(" ");
    }
    colNextLongHandled = true;
    requestUpdate();
  }

  if (input.colNext.released) {
    const bool wasHeld = colNextHeld;
    const bool edited = colNextLongHandled;
    colNextHeld = false;
    colNextLongHandled = false;
    rightHeld = false;
    rightLongHandled = false;
    if (wasHeld && !edited) {
      if (!cursorMode) {
        moveSelectionCol(1);
        requestUpdate();
      } else if (!togglePos && cursorPos < text.length()) {
        cursorPos = utf8Next(text, cursorPos);
        requestUpdate();
      }
    }
    if (cursorMode) return;
  }

  if (input.confirm.pressed) {
    confirmHeld = true;
    confirmLongHandled = false;
  }

  const fui::KeyboardKey* selKey = selectedKey();
  const bool selectedDel = selKey && selKey->value == fui::QWERTY_KEY_BACKSPACE;

  if (confirmHeld && !confirmLongHandled && input.confirm.held &&
      input.heldMs > DEL_LONG_PRESS_MS && selectedDel) {
    clearAllOrAltOnSelected();
    confirmLongHandled = true;
    requestUpdate();
  }

  if (confirmHeld && !confirmLongHandled && input.confirm.held &&
      input.heldMs > LONG_PRESS_MS) {
    if (!selectedDel && clearAllOrAltOnSelected()) {
      requestUpdate();
      confirmLongHandled = true;
    }
  }

  if (input.confirm.released) {
    if (confirmHeld && !confirmLongHandled && !cursorMode) {
      if (selKey && activateValue(selKey->value, false, complete)) {
        requestUpdate();
      }
    } else if (confirmHeld && !confirmLongHandled && cursorMode && inputType == InputType::Password && togglePos) {
      passwordVisible = !passwordVisible;
      requestUpdate();
    }
    confirmHeld = false;
    confirmLongHandled = false;
  }

  if (input.back.pressed) {
    backHeld = true;
    backLongHandled = false;
  }

  if (input.back.released) {
    backHeld = false;
    backLongHandled = false;
    complete = true;
  }

  if (hintVisible && !cursorMode && millis() - hintShowTime > 4000) {
    hintVisible = false;
    requestUpdate();
  }
}

void KeyboardEntryActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, title.c_str());

  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int inputStartY = inputTop();
  int inputHeight = 0;

  std::string displayText = displayTextForCurrentState();

  const bool isPassword = (inputType == InputType::Password);
  int availableWidth = pageWidth;
  if (gpio.deviceIsX3()) {
    availableWidth -= 2 * metrics.sideButtonHintsWidth;
  }
  const int effectiveMargin = (pageWidth - availableWidth * metrics.keyboardTextFieldWidthPercent / 100) / 2;
  const int toggleGap = isPassword ? 4 : 0;
  const int toggleReserve = isPassword ? std::max(renderer.getTextWidth(UI_12_FONT_ID, "[abc]"),
                                                  renderer.getTextWidth(UI_12_FONT_ID, "[***]")) +
                                             toggleGap
                                       : 0;
  const int textAreaWidth = pageWidth - 2 * effectiveMargin - toggleReserve;
  const int maxLineWidth = textAreaWidth;
  const bool centerText = metrics.keyboardCenteredText;

  // The cursor spans a whole code point: a lone byte of it renders as a replacement glyph.
  // Masking is per byte, so displayText keeps text's length and the same span applies to both.
  const size_t cursorCharBytes = (cursorPos < text.length()) ? utf8Next(text, cursorPos) - cursorPos : 0;
  char cursorChar[8] = {};         // the character under the cursor
  char displayCursorChar[8] = {};  // same span of displayText, masked for passwords
  if (cursorCharBytes > 0) {
    const size_t n = std::min(cursorCharBytes, sizeof(cursorChar) - 1);
    memcpy(cursorChar, text.data() + cursorPos, n);
    memcpy(displayCursorChar, displayText.data() + cursorPos, n);
  }

  int cursorCharWidth = 6;
  if (cursorCharBytes > 0) {
    int w = renderer.getTextWidth(UI_12_FONT_ID, cursorChar);
    if (w > cursorCharWidth) cursorCharWidth = w;
  }

  int lineStartIdx = inputWindowStart(displayText, maxLineWidth);
  int textWidth = 0;
  int cursorPixelX = effectiveMargin;
  int cursorLineY = inputStartY;
  bool cursorDrawn = false;

  while (true) {
    const int lineEndIdx = lineBreakEnd(displayText, lineStartIdx, maxLineWidth);
    const std::string lineText = displayText.substr(lineStartIdx, lineEndIdx - lineStartIdx);
    textWidth = renderer.getTextAdvanceX(UI_12_FONT_ID, lineText.c_str(), EpdFontFamily::REGULAR);
    {
      const bool isRtl = rangeIsRtl(displayText, lineStartIdx, lineEndIdx);
      const int lineStartX = centerText ? effectiveMargin + (maxLineWidth - textWidth) / 2 : effectiveMargin;
      const bool isLastLine = (lineEndIdx == static_cast<int>(displayText.length()));
      bool isCursorLine = false;
      if (!cursorDrawn && cursorPos >= lineStartIdx &&
          (isLastLine ? cursorPos <= lineEndIdx : cursorPos < lineEndIdx)) {
        std::string beforeCursor;
        if (isPassword && !passwordVisible && cursorMode) {
          beforeCursor = std::string(cursorPos - lineStartIdx, '*');
        } else {
          beforeCursor = displayText.substr(lineStartIdx, cursorPos - lineStartIdx);
        }
        int beforeWidth = renderer.getTextAdvanceX(UI_12_FONT_ID, beforeCursor.c_str(), EpdFontFamily::REGULAR);
        int throughCursorWidth = beforeWidth;
        int kernOffset = 0;
        if (cursorCharBytes > 0) {
          std::string beforeAndCursor = beforeCursor + displayCursorChar;
          throughCursorWidth = renderer.getTextAdvanceX(UI_12_FONT_ID, beforeAndCursor.c_str(), EpdFontFamily::REGULAR);
          int charAdvance = renderer.getTextAdvanceX(UI_12_FONT_ID, displayCursorChar, EpdFontFamily::REGULAR);
          kernOffset = throughCursorWidth - beforeWidth - charAdvance;
        }
        if (isRtl) {
          const int logicalWidth = cursorMode && cursorCharBytes > 0 ? throughCursorWidth : beforeWidth;
          cursorPixelX = lineStartX + textWidth - logicalWidth;
        } else {
          cursorPixelX = lineStartX + beforeWidth + kernOffset;
        }
        cursorLineY = inputStartY + inputHeight;
        cursorDrawn = true;
        isCursorLine = true;
      }

      if (isCursorLine && cursorMode && isPassword && !passwordVisible && !togglePos) {
        // Draw text in 3 parts to avoid block cursor overflowing onto next char.
        // displayText uses '*' for all chars; actual char may be wider than '*'.
        // Part 1: chars before cursor position
        const std::string part1 = displayText.substr(lineStartIdx, cursorPos - lineStartIdx);
        renderer.drawText(UI_12_FONT_ID, lineStartX, inputStartY + inputHeight, part1.c_str());
        // Part 2: skip cursor slot (block + actual char drawn later)
        // Part 3: chars after cursor position (skip char under cursor), starting at cursorPixelX + cursorCharWidth
        const int afterStart = static_cast<int>(cursorPos + cursorCharBytes);
        const int afterEnd = lineEndIdx;
        if (afterStart < afterEnd) {
          const std::string part3 = displayText.substr(afterStart, afterEnd - afterStart);
          renderer.drawText(UI_12_FONT_ID, cursorPixelX + cursorCharWidth, inputStartY + inputHeight, part3.c_str());
        }
      } else {
        renderer.drawText(UI_12_FONT_ID, lineStartX, inputStartY + inputHeight, lineText.c_str());
      }
      if (singleLineInput(renderer) || lineEndIdx == static_cast<int>(displayText.length())) {
        break;
      }

      inputHeight += lineHeight;
      lineStartIdx = lineEndIdx;
    }
  }

  const int fieldWidth = (inputHeight > 0) ? maxLineWidth : textWidth;
  const int lineMargin = effectiveMargin;
  GUI.drawTextField(renderer, Rect{0, inputStartY, pageWidth, inputHeight}, fieldWidth, cursorMode, lineMargin,
                    pageWidth - 2 * lineMargin);

  if (cursorMode && !togglePos && cursorPos <= displayText.length()) {
    static constexpr int blockPadding = 1;
    renderer.fillRect(cursorPixelX - blockPadding, cursorLineY, cursorCharWidth + blockPadding * 2, lineHeight, true);
    if (cursorCharBytes > 0) {
      renderer.drawText(UI_12_FONT_ID, cursorPixelX, cursorLineY, cursorChar, false);
    }
  } else if (cursorPos <= displayText.length()) {
    static constexpr int serifW = 3;
    const int cX = cursorPixelX;
    const int cY = cursorLineY;
    const int cBottom = cursorLineY + lineHeight - 1;
    renderer.fillRect(cX, cY, 2, lineHeight, true);
    renderer.drawLine(cX - serifW, cY, cX - 1, cY, 2, true);
    renderer.drawLine(cX + 1, cY, cX + serifW, cY, 2, true);
    renderer.drawLine(cX - serifW, cBottom, cX - 1, cBottom, 2, true);
    renderer.drawLine(cX + 1, cBottom, cX + serifW, cBottom, 2, true);
  }

  if (isPassword) {
    const char* toggleLabel = passwordVisible ? "[***]" : "[abc]";
    const int toggleWidth = renderer.getTextWidth(UI_12_FONT_ID, toggleLabel);
    const int toggleX = pageWidth - effectiveMargin - toggleWidth;
    const int toggleY = inputStartY + inputHeight;
    const bool toggleSelected = cursorMode && togglePos;

    if (toggleSelected) {
      renderer.fillRect(toggleX - 2, toggleY, toggleWidth + 5, lineHeight + 3, true);
      renderer.drawText(UI_12_FONT_ID, toggleX, toggleY, toggleLabel, false);
    } else {
      renderer.drawText(UI_12_FONT_ID, toggleX, toggleY, toggleLabel, true);
    }
  }

  // The tips below name front buttons; the touch shell has none.
  if (!tenorchrome::kTouchShell && normalizedUiTextSize(SETTINGS.uiTextSize) == 0 && hintVisible && !text.empty()) {
    const int hintLh = renderer.getLineHeight(SMALL_FONT_ID);
    const int underlineY = inputStartY + inputHeight + lineHeight + metrics.verticalSpacing;
    const int hintY = underlineY + 4;
    if (cursorMode) {
      int hintLineY = hintY;
      if (inputType == InputType::Password && togglePos) {
        renderer.drawCenteredText(
            SMALL_FONT_ID, hintLineY,
            passwordVisible ? tr(STR_KB_HINT_TOGGLE_HIDE_PASSWORD) : tr(STR_KB_HINT_TOGGLE_SHOW_PASSWORD), true);
        hintLineY += hintLh;
        renderer.drawCenteredText(SMALL_FONT_ID, hintLineY, tr(STR_KB_HINT_RETURN_CURSOR), true);
      } else {
        renderer.drawCenteredText(SMALL_FONT_ID, hintLineY, tr(STR_KB_HINT_MOVE_CURSOR), true);
        hintLineY += hintLh;
        if (inputType == InputType::Password) {
          const char* passTip = passwordVisible ? tr(STR_KB_HINT_HIDE_PASSWORD) : tr(STR_KB_HINT_SHOW_PASSWORD);
          renderer.drawCenteredText(SMALL_FONT_ID, hintLineY, passTip, true);
        }
      }
    } else {
      renderer.drawCenteredText(SMALL_FONT_ID, hintY, tr(STR_KB_HINT_EDIT_ENTRY), true);
    }
  }

  const fui::Rect kbRect = keyboardRect();

  const int tipsLh = renderer.getLineHeight(SMALL_FONT_ID);
  const int underlineBottom = inputStartY + inputHeight + lineHeight + metrics.verticalSpacing + 4;
  auto drawTip = [&](const char* tip, int y) { renderer.drawCenteredText(SMALL_FONT_ID, y, tip, true); };

  int tipCount = 0;
  if (cursorMode) {
    tipCount = 1;
  } else if (urlPanel) {
    tipCount = 1 + (!text.empty() ? 1 : 0);
  } else if (symbols) {
    tipCount = !text.empty() ? 1 : 0;
  } else {
    tipCount = 5 + (inputType == InputType::Url ? 1 : 0);
  }

  if (tenorchrome::kTouchShell) {
  } else if (normalizedUiTextSize(SETTINGS.uiTextSize) != 0) {
    const char* contextual = cursorMode ? tr(STR_KB_HINT_RETURN_KEYBOARD) : tr(STR_KB_HINT_EDIT_ENTRY);
    if (cursorMode && inputType == InputType::Password)
      contextual = passwordVisible ? tr(STR_KB_HINT_HIDE_PASSWORD) : tr(STR_KB_HINT_SHOW_PASSWORD);
    const int tipY = kbRect.y + kbRect.height + 6;
    const auto fit = [&](const char* text, int y) {
      renderer.drawCenteredText(SMALL_FONT_ID, y,
                                renderer.truncatedText(SMALL_FONT_ID, text, pageWidth - 24).c_str(), true);
    };
    fit(contextual, tipY);
    fit(tr(STR_KB_HINT_CLEAR_TEXT), tipY + tipsLh);
  } else if (tipCount > 0) {
    const char* tips[8];
    int n = 0;
    tips[n++] = shell::uglyParts() ? ugly::words::keyboardTips() : tr(STR_KB_TIPS);
    if (cursorMode) {
      tips[n++] = tr(STR_KB_HINT_RETURN_KEYBOARD);
    } else if (urlPanel) {
      tips[n++] = tr(STR_KB_HINT_EXIT_URL_MODE);
      if (!text.empty()) tips[n++] = tr(STR_KB_HINT_CLEAR_TEXT);
    } else if (symbols) {
      if (!text.empty()) tips[n++] = tr(STR_KB_HINT_CLEAR_TEXT);
    } else {
      if (inputType == InputType::Url) {
        tips[n++] = tr(STR_KB_HINT_SECONDARY_CHAR);
      } else if (shifted) {
        tips[n++] = tr(STR_KB_HINT_LOWER_SECONDARY);
      } else {
        tips[n++] = tr(STR_KB_HINT_UPPER_SECONDARY);
      }
      tips[n++] = tr(STR_KB_HINT_QUICK_SPACE);
      tips[n++] = tr(STR_KB_HINT_QUICK_BACKSPACE);
      tips[n++] = tr(STR_KB_HINT_EDIT_ENTRY);
      if (inputType == InputType::Url) tips[n++] = tr(STR_KB_HINT_URL_SNIPPETS);
      // Always shown: a tap on Back saves, and that is the one thing a reader
      // must know before typing, empty field or not.
      tips[n++] = tr(STR_KB_HINT_CLEAR_TEXT);
    }
    if (shell::uglyParts()) {
      // In hand, one tip a line over the key bar. The hand is taller than the small font: when the lines outgrow the
      // room under the keys, the title goes first, then the tips at the top; the one about Back stays.
      const int bottom = renderer.getScreenHeight() - metrics.buttonHintsHeight - 14;
      const int room = std::max(1, (bottom - (kbRect.y + kbRect.height) - 24) / 26 + 1);
      const int first = std::max(0, n - room);
      for (int i = first; i < n; ++i) tenorchrome::drawTip(renderer, tips[i], n - 1 - i);
    } else {
      int y = tenorchrome::enabled() ? tenorchrome::tipY(renderer) - tipCount * tipsLh
                                     : (underlineBottom + kbRect.y) / 2 - (tipCount + 1) * tipsLh / 2;
      for (int i = 0; i < n; ++i, y += tipsLh) drawTip(tips[i], y);
    }
  }

  // The FreeInkUI keyboard draws the keys and registers their hit rects into
  // `interactions`; loop() (the main task) routes touch snapshots against
  // that table via TouchHoldRouter, which reads the published generation
  // (routePublished()/publishedData()) - beginPublishCycle() here makes this
  // render build into the OTHER generation, so loop() never sees a
  // half-rebuilt table no matter when it runs relative to this.
  interactions.beginPublishCycle();
  fui::GfxRendererTarget target(renderer);
  target.setFont(fui::GfxRendererTarget::FONT_SMALL, SMALL_FONT_ID);
  target.setFont(fui::GfxRendererTarget::FONT_BODY, UI_12_FONT_ID);
#if defined(FREEINK_DEVICE_X4PRO)
  // Compact landscape rows keep both digit and alternate complete. Reuse the
  // existing fixed families without rebinding the user's input/status fonts.
  const int rowGap = renderer.getScreenWidth() > renderer.getScreenHeight() ? 0 : fui::KeyboardProps{}.rowGap;
  const int rowHeight = (kbRect.height - (currentLayout().rowCount - 1) * rowGap) /
                        currentLayout().rowCount;
  if (normalizedUiTextSize(SETTINGS.uiTextSize) != 0 &&
      rowHeight < renderer.getLineHeight(UI_12_FONT_ID) + renderer.getLineHeight(SMALL_FONT_ID) + 8) {
    constexpr int KEY_BODY_FONT_ID = 0x4b424f44, KEY_ALT_FONT_ID = 0x4b414c54;
    if (renderer.getFontMap().find(KEY_BODY_FONT_ID) == renderer.getFontMap().end()) {
      renderer.insertFont(KEY_BODY_FONT_ID, uiFontTierFamily(UIFontRole::Subtitle, 0));
      renderer.insertFont(KEY_ALT_FONT_ID, uiFontTierFamily(UIFontRole::Caption, 0));
    }
    target.setFont(fui::GfxRendererTarget::FONT_BODY, KEY_BODY_FONT_ID);
    target.setFont(fui::GfxRendererTarget::FONT_SMALL, KEY_ALT_FONT_ID);
  }
#endif
  const fui::DeviceContext device = target.deviceContext();
  const fui::InputSnapshot noInput{};
  fui::Frame<56> frame(target, device, noInput, interactions);

  fui::KeyboardProps props;
  const fui::KeyboardLayout& layout = currentLayout();
  props.layout = &layout;
  props.alignedColumns = SETTINGS.keyboardAligned != 0;
  props.keyAction = ACTION_KEY;  // one action id; loop() dispatches on key value
  props.okLabel = tr(STR_OK_BUTTON);
  props.shiftLabel = SETTINGS.keyboardAligned ? tr(STR_KEY_CASE_SHORT) : tr(STR_KEY_SHIFT);
  // Match the label to the layer the mode key leads back from: the symbols
  // layer and the URL snippet panel both label it "abc" in the static tables.
  props.modeLabel =
      (symbols || (inputType == InputType::Url && urlPanel)) ? tr(STR_KEY_MODE_ABC) : tr(STR_KEY_MODE_SYMBOLS);
  props.inputMask = static_cast<uint16_t>(fui::InputTouch | fui::InputLongPress);
  props.selectedIndex = cursorMode ? -1 : static_cast<int16_t>(selectedLogicalIndex());
  props.labelText.font = fui::GfxRendererTarget::FONT_BODY;
  props.altText.font = fui::GfxRendererTarget::FONT_SMALL;
  props.stackAlternates = normalizedUiTextSize(SETTINGS.uiTextSize) != 0;
  props.gap = static_cast<int16_t>(metrics.keyboardKeySpacing);
  if (tenorchrome::kTouchShell && renderer.getScreenWidth() > renderer.getScreenHeight()) {
    props.rowGap = 0;
    props.minTouchSize = 60;
  }
  // The key cursor is a leaf of a key's short side (keys of one unit: ten to the widest row);
  // unselected keys have no fill or border, so only the cursor shows it.
  if (metrics.roundedMarks) {
    const int rows = currentLayout().rowCount;
    const int keyHeight = (kbRect.height - (rows - 1) * props.gap) / std::max(rows, 1);
    const int keyWidth = (kbRect.width - 9 * props.gap) / 10;
    props.keyRadius = static_cast<uint8_t>(tenorradius::leaf(std::min(keyHeight, keyWidth)));
  }
  props.padding = fui::Insets{0, 0, 0, 0};
  // Fingers land low on the bottom row (occlusion) and there is no key below
  // to catch the miss - extend its hit band down to the button hints bar.
  const int hintsTop = renderer.getScreenHeight() -
                       (tenorchrome::kTouchShell ? tenorchrome::footBackReserve() : metrics.buttonHintsHeight);
  props.bottomHitOverflow = static_cast<int16_t>(std::max(0, hintsTop - (kbRect.y + kbRect.height)));
  const bool handwritten = shell::uglyParts();
  target.setPaintingEnabled(!handwritten);
  fui::keyboard(frame, kbRect, props);
  target.setPaintingEnabled(true);
  interactions.publish();
  interactionsReady = true;
  if (handwritten) {
    const int rows = std::max<int>(1, layout.rowCount);
    paintUglyKeys(renderer, props, interactions.publishedData(), interactions.publishedCount(),
                  (kbRect.height - props.rowGap * (rows - 1)) / rows);
  }

  // Hints follow the live axis, not the button names: with keyboardAxisSwapped
  // the front pair walks rows and the edge buttons walk columns. On the X3 the
  // edge buttons sit left and right even though the code calls them Up/Down,
  // and drawSideButtonHints draws its first argument on the left edge.
  const bool swapAxis = SETTINGS.keyboardAxisSwapped != 0;
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), swapAxis ? tr(STR_DIR_UP) : tr(STR_DIR_LEFT),
                                            swapAxis ? tr(STR_DIR_DOWN) : tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  // Side hints are drawn rotated, so the glyph handed over is not the glyph the
  // reader sees: passing "^" shows "<" and passing "v" shows ">". Measured off
  // the panel on 13/09/2026 rather than derived, the rotation helper reads as
  // clockwise but lands counter-clockwise on this board.
  GUI.drawSideButtonHints(renderer, swapAxis ? "^" : ">", swapAxis ? "v" : "<");

  renderer.displayBuffer();
}

void KeyboardEntryActivity::onComplete(std::string text) {
  setResult(KeyboardResult{std::move(text)});
  finish();
}

void KeyboardEntryActivity::onCancel() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

void KeyboardEntryActivity::onTimeout() {
  ActivityResult result{KeyboardResult{"", true}};
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}
