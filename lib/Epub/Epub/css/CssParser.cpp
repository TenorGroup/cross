#include "CssParser.h"

#include <Logging.h>
#include <HeapFloor.h>
#include <Memory.h>
#ifdef TENOR_PRESS_PROBE
#include <Arduino.h>
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstring>
#include <string_view>
#include <type_traits>

#include "CssNumber.h"

namespace {

// Stack-allocated string buffer to avoid heap reallocations during parsing
// Provides string-like interface with fixed capacity
struct StackBuffer {
  static constexpr size_t CAPACITY = 1024;
  char data[CAPACITY];
  size_t len = 0;

  bool push_back(char c) {
    if (len >= CAPACITY) return false;
    data[len++] = c;
    return true;
  }

  void clear() { len = 0; }
  bool empty() const { return len == 0; }
  size_t size() const { return len; }

  // Get string view of current content (zero-copy)
  std::string_view view() const { return std::string_view(data, len); }
  operator std::string_view() const noexcept { return view(); }
};

// Buffer size for reading CSS files
constexpr size_t READ_BUFFER_SIZE = 512;

// Flat rule-store caps. The index is 12KB at MAX_RULES, selector text is
// bounded to 32KB, and deduplicated style bodies are bounded to about 26KB.
constexpr size_t MAX_RULES = 1500;
constexpr size_t SELECTOR_POOL_CAP = 32 * 1024;
constexpr size_t MAX_UNIQUE_STYLES = 256;

// Maximum length for a single selector string
// Prevents parsing of extremely long or malformed selectors
constexpr size_t MAX_SELECTOR_LENGTH = 256;

// Check if character is CSS whitespace
constexpr bool isCssWhitespace(const char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }

constexpr std::string_view trimCssWhitespace(std::string_view s) {
  while (!s.empty() && isCssWhitespace(s.front())) s.remove_prefix(1);
  while (!s.empty() && isCssWhitespace(s.back())) s.remove_suffix(1);
  return s;
}

constexpr char asciiToLower(const char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c; }

// Case-insensitive equality on ASCII. lowercaseKeyword MUST already be
// lowercase; CSS keywords are ASCII by spec so byte-wise tolower is safe.
constexpr bool iequalsAscii(std::string_view value, std::string_view lowercaseKeyword) {
  return std::equal(value.begin(), value.end(), lowercaseKeyword.begin(), lowercaseKeyword.end(),
                    [](char a, char b) { return asciiToLower(a) == b; });
}

// Walk s and invoke fn(token) for each non-empty run between delimiters.
// Tokens are boundary-trimmed and yielded as string_views into s; no
// allocation. Runs of consecutive delimiters coalesce — no empty tokens are
// emitted. `isDelimiter` is invoked once per character.
template <typename Pred, typename F>
void forEachDelimitedToken(std::string_view s, Pred isDelimiter, F&& fn) {
  size_t start = 0;
  for (size_t i = 0; i <= s.size(); ++i) {
    if (i == s.size() || isDelimiter(s[i])) {
      const std::string_view trimmed = trimCssWhitespace(s.substr(start, i - start));
      if (!trimmed.empty()) {
        fn(trimmed);
      }
      start = i + 1;
    }
  }
}

// Parse the entirety of s as a number into `out`. Accepts an optional leading
// '+' (which std::from_chars rejects by spec) so callers can pass CSS-style
// signed numbers without manual trimming. Returns false on empty input, a
// non-numeric suffix, or any from_chars error. A float goes through
// cssnumber::parse, which gives what std::from_chars gives for the numbers
// tryInterpretLength hands over, without libstdc++'s 21 KB float parser.
template <typename T>
bool tryParseNumber(std::string_view s, T& out) {
  const char* begin = s.data();
  const char* end = s.data() + s.size();
  if (begin < end && *begin == '+') ++begin;
  if constexpr (std::is_same_v<T, float>) {
    return cssnumber::parse(begin, end, out);
  } else {
    const auto r = std::from_chars(begin, end, out);
    return r.ec == std::errc{} && r.ptr == end;
  }
}

// Collect up to 4 whitespace-separated tokens for a CSS edge-value shorthand
// (margin, padding, and the border-* family). Returns the number of tokens
// written; extras are silently dropped. Callers apply the 1/2/3/4-value
// fallback rule using the returned count.
size_t collectEdgeValueTokens(std::string_view s, std::string_view (&out)[4]) {
  size_t count = 0;
  forEachDelimitedToken(s, isCssWhitespace, [&](std::string_view tok) {
    if (count < 4) out[count++] = tok;
  });
  return count;
}

std::string_view stripTrailingImportant(std::string_view value) {
  constexpr std::string_view IMPORTANT = "!important";

  while (!value.empty() && isCssWhitespace(value.back())) {
    value.remove_suffix(1);
  }

  if (value.size() < IMPORTANT.size()) {
    return value;
  }

  const size_t suffixPos = value.size() - IMPORTANT.size();
  if (!iequalsAscii(value.substr(suffixPos), IMPORTANT)) {
    return value;
  }

  value.remove_suffix(IMPORTANT.size());
  while (!value.empty() && isCssWhitespace(value.back())) {
    value.remove_suffix(1);
  }
  return value;
}

constexpr std::array STYLE_LENGTH_FIELDS = {
    &CssStyle::textIndent,   &CssStyle::marginTop,   &CssStyle::marginBottom,  &CssStyle::marginLeft,
    &CssStyle::marginRight,  &CssStyle::paddingTop,  &CssStyle::paddingBottom, &CssStyle::paddingLeft,
    &CssStyle::paddingRight, &CssStyle::imageHeight, &CssStyle::imageWidth,
};
constexpr size_t STYLE_LENGTH_FIELD_COUNT = STYLE_LENGTH_FIELDS.size();
constexpr size_t STYLE_WIRE_BYTES =
    5 + STYLE_LENGTH_FIELD_COUNT * (sizeof(decltype(CssLength::value)) + 1) + 3 + sizeof(uint32_t);
constexpr uint32_t CSS_DEFINED_BITS_MASK = (1u << 19) - 1;

void encodeStyleWire(const CssStyle& style, uint8_t (&out)[STYLE_WIRE_BYTES]) {
  size_t offset = 0;
  out[offset++] = static_cast<uint8_t>(style.textAlign);
  out[offset++] = static_cast<uint8_t>(style.fontStyle);
  out[offset++] = static_cast<uint8_t>(style.fontWeight);
  out[offset++] = static_cast<uint8_t>(style.textDecoration);
  out[offset++] = static_cast<uint8_t>(style.direction);

  const auto putLength = [&out, &offset](const CssLength& length) {
    memcpy(out + offset, &length.value, sizeof(length.value));
    offset += sizeof(length.value);
    out[offset++] = static_cast<uint8_t>(length.unit);
  };
  for (const auto field : STYLE_LENGTH_FIELDS) {
    putLength(style.*field);
  }
  out[offset++] = static_cast<uint8_t>(style.display);
  out[offset++] = static_cast<uint8_t>(style.verticalAlign);
  out[offset++] = static_cast<uint8_t>(style.listStyleType);

  uint32_t definedBits = 0;
  if (style.defined.textAlign) definedBits |= 1 << 0;
  if (style.defined.fontStyle) definedBits |= 1 << 1;
  if (style.defined.fontWeight) definedBits |= 1 << 2;
  if (style.defined.textDecoration) definedBits |= 1 << 3;
  if (style.defined.textIndent) definedBits |= 1 << 4;
  if (style.defined.marginTop) definedBits |= 1 << 5;
  if (style.defined.marginBottom) definedBits |= 1 << 6;
  if (style.defined.marginLeft) definedBits |= 1 << 7;
  if (style.defined.marginRight) definedBits |= 1 << 8;
  if (style.defined.paddingTop) definedBits |= 1 << 9;
  if (style.defined.paddingBottom) definedBits |= 1 << 10;
  if (style.defined.paddingLeft) definedBits |= 1 << 11;
  if (style.defined.paddingRight) definedBits |= 1 << 12;
  if (style.defined.imageHeight) definedBits |= 1 << 13;
  if (style.defined.imageWidth) definedBits |= 1 << 14;
  if (style.defined.display) definedBits |= 1 << 15;
  if (style.defined.direction) definedBits |= 1 << 16;
  if (style.defined.verticalAlign) definedBits |= 1 << 17;
  if (style.defined.listStyleType) definedBits |= 1 << 18;
  memcpy(out + offset, &definedBits, sizeof(definedBits));
}

bool decodeStyleWire(const uint8_t (&in)[STYLE_WIRE_BYTES], CssStyle& style) {
  size_t offset = 0;
  const uint8_t textAlign = in[offset++];
  const uint8_t fontStyle = in[offset++];
  const uint8_t fontWeight = in[offset++];
  const uint8_t textDecoration = in[offset++];
  const uint8_t direction = in[offset++];
  if (textAlign > static_cast<uint8_t>(CssTextAlign::None) || fontStyle > static_cast<uint8_t>(CssFontStyle::Italic) ||
      fontWeight > static_cast<uint8_t>(CssFontWeight::Bold) || (textDecoration & ~CSS_TEXT_DECORATION_MASK) != 0 ||
      direction > static_cast<uint8_t>(CssTextDirection::Rtl)) {
    return false;
  }
  style.textAlign = static_cast<CssTextAlign>(textAlign);
  style.fontStyle = static_cast<CssFontStyle>(fontStyle);
  style.fontWeight = static_cast<CssFontWeight>(fontWeight);
  style.textDecoration = static_cast<CssTextDecoration>(textDecoration);
  style.direction = static_cast<CssTextDirection>(direction);

  const auto getLength = [&in, &offset](CssLength& length) {
    decltype(CssLength::value) value = 0;
    memcpy(&value, in + offset, sizeof(value));
    offset += sizeof(length.value);
    const uint8_t unit = in[offset++];
    if (!std::isfinite(value) || unit > static_cast<uint8_t>(CssUnit::Percent)) return false;
    length.value = value;
    length.unit = static_cast<CssUnit>(unit);
    return true;
  };
  for (const auto field : STYLE_LENGTH_FIELDS) {
    if (!getLength(style.*field)) return false;
  }

  const uint8_t display = in[offset++];
  const uint8_t verticalAlign = in[offset++];
  const uint8_t listStyleType = in[offset++];
  if (display > static_cast<uint8_t>(CssDisplay::None) || verticalAlign > static_cast<uint8_t>(CssVerticalAlign::Sub) ||
      listStyleType > static_cast<uint8_t>(CssListStyleType::None)) {
    return false;
  }
  style.display = static_cast<CssDisplay>(display);
  style.verticalAlign = static_cast<CssVerticalAlign>(verticalAlign);
  style.listStyleType = static_cast<CssListStyleType>(listStyleType);

  uint32_t definedBits = 0;
  memcpy(&definedBits, in + offset, sizeof(definedBits));
  if ((definedBits & ~CSS_DEFINED_BITS_MASK) != 0) return false;
  style.defined.textAlign = (definedBits & 1 << 0) != 0;
  style.defined.fontStyle = (definedBits & 1 << 1) != 0;
  style.defined.fontWeight = (definedBits & 1 << 2) != 0;
  style.defined.textDecoration = (definedBits & 1 << 3) != 0;
  style.defined.textIndent = (definedBits & 1 << 4) != 0;
  style.defined.marginTop = (definedBits & 1 << 5) != 0;
  style.defined.marginBottom = (definedBits & 1 << 6) != 0;
  style.defined.marginLeft = (definedBits & 1 << 7) != 0;
  style.defined.marginRight = (definedBits & 1 << 8) != 0;
  style.defined.paddingTop = (definedBits & 1 << 9) != 0;
  style.defined.paddingBottom = (definedBits & 1 << 10) != 0;
  style.defined.paddingLeft = (definedBits & 1 << 11) != 0;
  style.defined.paddingRight = (definedBits & 1 << 12) != 0;
  style.defined.imageHeight = (definedBits & 1 << 13) != 0;
  style.defined.imageWidth = (definedBits & 1 << 14) != 0;
  style.defined.display = (definedBits & 1 << 15) != 0;
  style.defined.direction = (definedBits & 1 << 16) != 0;
  style.defined.verticalAlign = (definedBits & 1 << 17) != 0;
  style.defined.listStyleType = (definedBits & 1 << 18) != 0;
  return true;
}

}  // anonymous namespace

int CssParser::compareEntryToPieces(const SelectorEntry& entry, const std::string_view p0, const std::string_view p1,
                                    const std::string_view p2) const {
  const char* stored = selectorPool_.get() + entry.offset;
  const std::string_view pieces[] = {p0, p1, p2};
  size_t index = 0;
  for (const std::string_view piece : pieces) {
    for (const char c : piece) {
      if (index == entry.length) return -1;
      const auto storedByte = static_cast<unsigned char>(stored[index]);
      const auto probeByte = static_cast<unsigned char>(asciiToLower(c));
      if (storedByte != probeByte) return storedByte < probeByte ? -1 : 1;
      ++index;
    }
  }
  return index == entry.length ? 0 : 1;
}

size_t CssParser::lowerBound(const std::string_view p0, const std::string_view p1, const std::string_view p2,
                             bool& exact) const {
  size_t low = 0;
  size_t high = entryCount_;
  while (low < high) {
    const size_t middle = low + (high - low) / 2;
    if (compareEntryToPieces(entries_[middle], p0, p1, p2) < 0) {
      low = middle + 1;
    } else {
      high = middle;
    }
  }
  exact = low < entryCount_ && compareEntryToPieces(entries_[low], p0, p1, p2) == 0;
  return low;
}

const CssStyle* CssParser::findStyle(const std::string_view p0, const std::string_view p1,
                                     const std::string_view p2) const {
  bool exact = false;
  const size_t index = lowerBound(p0, p1, p2, exact);
  return exact ? &stylePool_[entries_[index].styleIndex] : nullptr;
}

std::string_view CssParser::selectorAt(const size_t index) const {
  const SelectorEntry& entry = entries_[index];
  return {selectorPool_.get() + entry.offset, entry.length};
}

CssParser::PoolResult CssParser::ensureCapacity(const size_t entries, const size_t selectors, const size_t styles) {
  if (entries > MAX_RULES || selectors > SELECTOR_POOL_CAP || styles > MAX_UNIQUE_STYLES) return PoolResult::Limit;
  const auto capacityFor = [](size_t current, const size_t needed, const size_t initial, const size_t limit) {
    if (needed <= current) return current;
    size_t capacity = current ? current * 2 : initial;
    while (capacity < needed) capacity *= 2;
    return std::min(capacity, limit);
  };
  const size_t entriesCapacity = capacityFor(entryCapacity_, entries, 128, MAX_RULES);
  const size_t selectorsCapacity = capacityFor(selectorPoolCapacity_, selectors, 4096, SELECTOR_POOL_CAP);
  const size_t stylesCapacity = capacityFor(styleCapacity_, styles, 16, MAX_UNIQUE_STYLES);
  std::unique_ptr<SelectorEntry[]> grownEntries;
  std::unique_ptr<char[]> grownSelectors;
  std::unique_ptr<CssStyle[]> grownStyles;
  if (entriesCapacity != entryCapacity_) grownEntries = heapfloor::array<SelectorEntry[]>(entriesCapacity, "css-entries");
  if (entriesCapacity != entryCapacity_ && !grownEntries) return PoolResult::OutOfMemory;
  if (selectorsCapacity != selectorPoolCapacity_) grownSelectors = heapfloor::array<char[]>(selectorsCapacity, "css-selectors");
  if (selectorsCapacity != selectorPoolCapacity_ && !grownSelectors) return PoolResult::OutOfMemory;
  if (stylesCapacity != styleCapacity_) grownStyles = heapfloor::array<CssStyle[]>(stylesCapacity, "css-styles");
  if (stylesCapacity != styleCapacity_ && !grownStyles) return PoolResult::OutOfMemory;

  // Commit together so a refused rule gives every temporary allocation back.
  if (grownEntries) {
    if (entryCount_) memcpy(grownEntries.get(), entries_.get(), entryCount_ * sizeof(SelectorEntry));
    entries_ = std::move(grownEntries);
    entryCapacity_ = static_cast<uint16_t>(entriesCapacity);
  }
  if (grownSelectors) {
    if (selectorPoolSize_) memcpy(grownSelectors.get(), selectorPool_.get(), selectorPoolSize_);
    selectorPool_ = std::move(grownSelectors);
    selectorPoolCapacity_ = static_cast<uint32_t>(selectorsCapacity);
  }
  if (grownStyles) {
    for (size_t i = 0; i < styleCount_; ++i) grownStyles[i] = stylePool_[i];
    stylePool_ = std::move(grownStyles);
    styleCapacity_ = static_cast<uint16_t>(stylesCapacity);
  }
  return PoolResult::Ready;
}

int CssParser::existingStyleIndex(const CssStyle& style) const {
  uint8_t wire[STYLE_WIRE_BYTES];
  encodeStyleWire(style, wire);
  for (uint16_t i = 0; i < styleCount_; ++i) {
    uint8_t existingWire[STYLE_WIRE_BYTES];
    encodeStyleWire(stylePool_[i], existingWire);
    if (memcmp(existingWire, wire, STYLE_WIRE_BYTES) == 0) return i;
  }
  return -1;
}

CssParser::PoolResult CssParser::internStyle(const CssStyle& style, uint16_t& indexOut) {
  const int existing = existingStyleIndex(style);
  if (existing >= 0) {
    indexOut = static_cast<uint16_t>(existing);
    return PoolResult::Ready;
  }
  const PoolResult result = ensureCapacity(entryCount_, selectorPoolSize_, static_cast<size_t>(styleCount_) + 1);
  if (result != PoolResult::Ready) return result;
  stylePool_[styleCount_] = style;
  indexOut = styleCount_++;
  return PoolResult::Ready;
}

CssParser::RuleInsertResult CssParser::insertOrMerge(const std::string_view selector, const CssStyle& style) {
  bool exact = false;
  const size_t position = lowerBound(selector, {}, {}, exact);
  if (exact) {
    const uint16_t currentStyleIndex = entries_[position].styleIndex;
    CssStyle merged = stylePool_[currentStyleIndex];
    merged.applyOver(style);

    bool styleIsShared = false;
    for (uint16_t i = 0; i < entryCount_; ++i) {
      if (i != position && entries_[i].styleIndex == currentStyleIndex) {
        styleIsShared = true;
        break;
      }
    }
    if (!styleIsShared) {
      stylePool_[currentStyleIndex] = merged;
      return RuleInsertResult::Merged;
    }

    uint16_t styleIndex = 0;
    const PoolResult result = internStyle(merged, styleIndex);
    if (result == PoolResult::Limit) return RuleInsertResult::Limit;
    if (result == PoolResult::OutOfMemory) return RuleInsertResult::OutOfMemory;
    entries_[position].styleIndex = styleIndex;
    return RuleInsertResult::Merged;
  }

  const size_t requiredSelectorBytes = static_cast<size_t>(selectorPoolSize_) + selector.size();
  const int existing = existingStyleIndex(style);
  const PoolResult result = ensureCapacity(static_cast<size_t>(entryCount_) + 1, requiredSelectorBytes,
                                          static_cast<size_t>(styleCount_) + (existing < 0 ? 1 : 0));
  if (result == PoolResult::Limit) return RuleInsertResult::Limit;
  if (result == PoolResult::OutOfMemory) return RuleInsertResult::OutOfMemory;
  const uint16_t styleIndex = existing >= 0 ? static_cast<uint16_t>(existing) : styleCount_++;
  if (existing < 0) stylePool_[styleIndex] = style;

  const uint32_t selectorOffset = selectorPoolSize_;
  char* destination = selectorPool_.get() + selectorOffset;
  for (const char c : selector) *destination++ = asciiToLower(c);
  selectorPoolSize_ = static_cast<uint32_t>(requiredSelectorBytes);

  SelectorEntry* entries = entries_.get();
  memmove(entries + position + 1, entries + position, (entryCount_ - position) * sizeof(SelectorEntry));
  entries[position] = {selectorOffset, styleIndex, static_cast<uint16_t>(selector.size())};
  ++entryCount_;
  return RuleInsertResult::Inserted;
}

// Property value interpreters

CssTextAlign CssParser::interpretAlignment(std::string_view val) {
  val = trimCssWhitespace(val);

  if (iequalsAscii(val, "left") || iequalsAscii(val, "start")) return CssTextAlign::Left;
  if (iequalsAscii(val, "right") || iequalsAscii(val, "end")) return CssTextAlign::Right;
  if (iequalsAscii(val, "center")) return CssTextAlign::Center;
  if (iequalsAscii(val, "justify")) return CssTextAlign::Justify;

  return CssTextAlign::Left;
}

CssFontStyle CssParser::interpretFontStyle(std::string_view val) {
  val = trimCssWhitespace(val);

  if (iequalsAscii(val, "italic") || iequalsAscii(val, "oblique")) return CssFontStyle::Italic;
  return CssFontStyle::Normal;
}

CssFontWeight CssParser::interpretFontWeight(std::string_view val) {
  val = trimCssWhitespace(val);

  // Named values
  if (iequalsAscii(val, "bold") || iequalsAscii(val, "bolder")) return CssFontWeight::Bold;
  if (iequalsAscii(val, "normal") || iequalsAscii(val, "lighter")) return CssFontWeight::Normal;

  // Numeric values: 100-900
  // CSS spec: 400 = normal, 700 = bold
  // We use: 0-400 = normal, 700+ = bold, 500-600 = normal (conservative)
  long numericWeight = 0;
  if (tryParseNumber(val, numericWeight)) {
    return numericWeight >= 700 ? CssFontWeight::Bold : CssFontWeight::Normal;
  }
  return CssFontWeight::Normal;
}

CssTextDecoration CssParser::interpretDecoration(std::string_view val) {
  // text-decoration can have multiple space-separated values. Compare whole tokens
  // so malformed values like "notunderline" do not accidentally enable a line.
  CssTextDecoration result = CssTextDecoration::None;
  bool explicitNone = false;
  forEachDelimitedToken(val, isCssWhitespace, [&](const std::string_view token) {
    if (iequalsAscii(token, "none")) {
      explicitNone = true;
    } else if (iequalsAscii(token, "underline")) {
      result = result | CssTextDecoration::Underline;
    } else if (iequalsAscii(token, "line-through")) {
      result = result | CssTextDecoration::LineThrough;
    }
  });
  return explicitNone ? CssTextDecoration::None : result;
}

CssLength CssParser::interpretLength(std::string_view val) {
  CssLength result;
  tryInterpretLength(val, result);
  return result;
}

bool CssParser::tryInterpretLength(std::string_view val, CssLength& out) {
  val = trimCssWhitespace(val);
  if (val.empty()) {
    out = CssLength{};
    return false;
  }

  size_t unitStart = val.size();
  for (size_t i = 0; i < val.size(); ++i) {
    const char c = val[i];
    if (!std::isdigit(c) && c != '.' && c != '-' && c != '+') {
      unitStart = i;
      break;
    }
  }

  float numericValue;
  if (!tryParseNumber(val.substr(0, unitStart), numericValue)) {
    out = CssLength{};
    return false;  // No number parsed (e.g. auto, inherit, initial)
  }

  const std::string_view unitPart = val.substr(unitStart);
  auto unit = CssUnit::Pixels;
  if (iequalsAscii(unitPart, "em")) {
    unit = CssUnit::Em;
  } else if (iequalsAscii(unitPart, "rem")) {
    unit = CssUnit::Rem;
  } else if (iequalsAscii(unitPart, "pt")) {
    unit = CssUnit::Points;
  } else if (unitPart == "%") {
    unit = CssUnit::Percent;
  }

  out = CssLength{numericValue, unit};
  return true;
}

// Declaration parsing

void CssParser::parseDeclarationIntoStyle(std::string_view decl, CssStyle& style) {
  const size_t colonPos = decl.find(':');
  if (colonPos == std::string_view::npos || colonPos == 0) return;

  const std::string_view name = trimCssWhitespace(decl.substr(0, colonPos));
  std::string_view value = trimCssWhitespace(decl.substr(colonPos + 1));

  if (name.empty() || value.empty()) return;

  value = stripTrailingImportant(value);

  if (iequalsAscii(name, "text-align")) {
    style.textAlign = interpretAlignment(value);
    style.defined.textAlign = 1;
  } else if (iequalsAscii(name, "font-style")) {
    style.fontStyle = interpretFontStyle(value);
    style.defined.fontStyle = 1;
  } else if (iequalsAscii(name, "font-weight")) {
    style.fontWeight = interpretFontWeight(value);
    style.defined.fontWeight = 1;
  } else if (iequalsAscii(name, "text-decoration") || iequalsAscii(name, "text-decoration-line")) {
    style.textDecoration = interpretDecoration(value);
    style.defined.textDecoration = 1;
  } else if (iequalsAscii(name, "text-indent")) {
    style.textIndent = interpretLength(value);
    style.defined.textIndent = 1;
  } else if (iequalsAscii(name, "margin-top")) {
    style.marginTop = interpretLength(value);
    style.defined.marginTop = 1;
  } else if (iequalsAscii(name, "margin-bottom")) {
    style.marginBottom = interpretLength(value);
    style.defined.marginBottom = 1;
  } else if (iequalsAscii(name, "margin-left")) {
    style.marginLeft = interpretLength(value);
    style.defined.marginLeft = 1;
  } else if (iequalsAscii(name, "margin-right")) {
    style.marginRight = interpretLength(value);
    style.defined.marginRight = 1;
  } else if (iequalsAscii(name, "margin")) {
    std::string_view margins[4];
    const size_t count = collectEdgeValueTokens(value, margins);
    if (count > 0) {
      style.marginTop = interpretLength(margins[0]);
      style.marginRight = count >= 2 ? interpretLength(margins[1]) : style.marginTop;
      style.marginBottom = count >= 3 ? interpretLength(margins[2]) : style.marginTop;
      style.marginLeft = count >= 4 ? interpretLength(margins[3]) : style.marginRight;
      style.defined.marginTop = style.defined.marginRight = style.defined.marginBottom = style.defined.marginLeft = 1;
    }
  } else if (iequalsAscii(name, "padding-top")) {
    style.paddingTop = interpretLength(value);
    style.defined.paddingTop = 1;
  } else if (iequalsAscii(name, "padding-bottom")) {
    style.paddingBottom = interpretLength(value);
    style.defined.paddingBottom = 1;
  } else if (iequalsAscii(name, "padding-left")) {
    style.paddingLeft = interpretLength(value);
    style.defined.paddingLeft = 1;
  } else if (iequalsAscii(name, "padding-right")) {
    style.paddingRight = interpretLength(value);
    style.defined.paddingRight = 1;
  } else if (iequalsAscii(name, "padding")) {
    std::string_view paddings[4];
    const size_t count = collectEdgeValueTokens(value, paddings);
    if (count > 0) {
      style.paddingTop = interpretLength(paddings[0]);
      style.paddingRight = count >= 2 ? interpretLength(paddings[1]) : style.paddingTop;
      style.paddingBottom = count >= 3 ? interpretLength(paddings[2]) : style.paddingTop;
      style.paddingLeft = count >= 4 ? interpretLength(paddings[3]) : style.paddingRight;
      style.defined.paddingTop = style.defined.paddingRight = style.defined.paddingBottom = style.defined.paddingLeft =
          1;
    }
  } else if (iequalsAscii(name, "height")) {
    CssLength len;
    if (tryInterpretLength(value, len)) {
      style.imageHeight = len;
      style.defined.imageHeight = 1;
    }
  } else if (iequalsAscii(name, "width")) {
    CssLength len;
    if (tryInterpretLength(value, len)) {
      style.imageWidth = len;
      style.defined.imageWidth = 1;
    }
  } else if (iequalsAscii(name, "display")) {
    style.display = iequalsAscii(value, "none") ? CssDisplay::None : CssDisplay::Block;
    style.defined.display = 1;
  } else if (iequalsAscii(name, "direction")) {
    if (iequalsAscii(value, "rtl")) {
      style.direction = CssTextDirection::Rtl;
      style.defined.direction = 1;
    } else if (iequalsAscii(value, "ltr")) {
      style.direction = CssTextDirection::Ltr;
      style.defined.direction = 1;
    }
  } else if (iequalsAscii(name, "vertical-align")) {
    if (iequalsAscii(value, "super")) {
      style.verticalAlign = CssVerticalAlign::Super;
      style.defined.verticalAlign = 1;
    } else if (iequalsAscii(value, "sub")) {
      style.verticalAlign = CssVerticalAlign::Sub;
      style.defined.verticalAlign = 1;
    }
  } else if (iequalsAscii(name, "list-style-type")) {
    const std::string_view listStyleValue = stripTrailingImportant(value);
    style.listStyleType = iequalsAscii(listStyleValue, "none") ? CssListStyleType::None : CssListStyleType::Disc;
    style.defined.listStyleType = 1;
  }
}

CssStyle CssParser::parseDeclarations(std::string_view declBlock) {
  CssStyle style;

  size_t start = 0;
  for (size_t i = 0; i <= declBlock.size(); ++i) {
    if (i == declBlock.size() || declBlock[i] == ';') {
      if (i > start) {
        parseDeclarationIntoStyle(declBlock.substr(start, i - start), style);
      }
      start = i + 1;
    }
  }

  return style;
}

// Rule processing

void CssParser::processRuleBlockWithStyle(std::string_view selectorGroup, const CssStyle& style) {
  // Skip rules that don't define any supported properties to save RAM.
  if (!style.defined.anySet()) {
    return;
  }

  // Walk comma-separated selectors in place. The bounded store reports every
  // capacity or allocation failure without crossing a throwing STL boundary.
  forEachDelimitedToken(
      selectorGroup, [](char c) { return c == ','; },
      [&](std::string_view sel) {
        if (sel.size() > MAX_SELECTOR_LENGTH) {
          LOG_DBG("CSS", "Selector too long (%zu > %zu), skipping", sel.size(), MAX_SELECTOR_LENGTH);
          return;
        }

        // TODO: Support richer CSS selector syntax in the future. For now we only
        // handle `tag`, `.class`, or `tag.class`. Reject anything containing a
        // character that introduces unsupported syntax:
        //   '+'  adjacent sibling combinator
        //   '>'  child combinator
        //   '['  attribute selector
        //   ':'  pseudo class/element
        //   '#'  ID selector
        //   '~'  general sibling combinator
        //   '*'  wildcard
        //   ' '  descendant combinator
        // Single-pass scan via find_first_of instead of eight sequential find() calls.
        constexpr std::string_view kUnsupportedSelectorChars = "+>[:#~* ";
        if (sel.find_first_of(kUnsupportedSelectorChars) != std::string_view::npos) return;

        if (ruleGrowthStopped_) {
          // Continue the cascade for stored selectors without retrying failed
          // allocations for new rules.
          bool exact = false;
          const size_t matchingIndex = lowerBound(sel, {}, {}, exact);
          if (!exact || matchingIndex >= entryCount_) return;
        }
        const RuleInsertResult result = insertOrMerge(sel, style);
        if (result == RuleInsertResult::Limit) {
          LOG_ERR("CSS", "CSS rule store limit reached at %u rules", entryCount_);
          ruleGrowthStopped_ = true;
        } else if (result == RuleInsertResult::OutOfMemory) {
          LOG_ERR("CSS", "OOM while growing CSS rule store at %u rules", entryCount_);
          ruleGrowthStopped_ = true;
        }
      });
}

// Main parsing entry point

CssParser::ParseResult CssParser::loadFromStream(HalFile& source) {
  if (!source) {
    LOG_ERR("CSS", "Cannot read from invalid file");
    return ParseResult::Error;
  }

  size_t totalRead = 0;

  // Use stack-allocated buffers for parsing to avoid heap reallocations
  StackBuffer selector;
  StackBuffer declBuffer;

  bool inComment = false;
  bool maybeSlash = false;
  bool prevStar = false;

  bool inAtRule = false;
  int atDepth = 0;

  int bodyDepth = 0;
  bool skippingRule = false;
  bool selectorTruncated = false;
  bool declarationTruncated = false;
  bool inputTruncated = false;
  CssStyle currentStyle;

  auto handleChar = [&](const char c) {
    if (inAtRule) {
      if (c == '{') {
        ++atDepth;
      } else if (c == '}') {
        if (atDepth > 0) --atDepth;
        if (atDepth == 0) inAtRule = false;
      } else if (c == ';' && atDepth == 0) {
        inAtRule = false;
      }
      return;
    }

    if (bodyDepth == 0) {
      if (selector.empty() && isCssWhitespace(c)) {
        return;
      }
      if (c == '@' && selector.empty()) {
        inAtRule = true;
        atDepth = 0;
        return;
      }
      if (c == '{') {
        bodyDepth = 1;
        currentStyle = CssStyle{};
        declBuffer.clear();
        skippingRule = selectorTruncated || selector.size() > MAX_SELECTOR_LENGTH * 4;
        return;
      }
      if (!selector.push_back(c)) {
        selectorTruncated = true;
        inputTruncated = true;
      }
      return;
    }

    // bodyDepth > 0
    if (c == '{') {
      ++bodyDepth;
      return;
    }
    if (c == '}') {
      --bodyDepth;
      if (bodyDepth == 0) {
        if (!skippingRule && !declarationTruncated && !declBuffer.empty()) {
          parseDeclarationIntoStyle(declBuffer, currentStyle);
        }
        if (!skippingRule) {
          processRuleBlockWithStyle(selector, currentStyle);
        }
        selector.clear();
        declBuffer.clear();
        skippingRule = false;
        selectorTruncated = false;
        declarationTruncated = false;
        return;
      }
      return;
    }
    if (bodyDepth > 1) {
      return;
    }
    if (!skippingRule) {
      if (c == ';') {
        if (!declarationTruncated && !declBuffer.empty()) {
          parseDeclarationIntoStyle(declBuffer, currentStyle);
        }
        declBuffer.clear();
        declarationTruncated = false;
      } else {
        if (!declBuffer.push_back(c)) {
          declarationTruncated = true;
          inputTruncated = true;
        }
      }
    }
  };

  char buffer[READ_BUFFER_SIZE];
  while (source.available()) {
    int bytesRead = source.read(buffer, sizeof(buffer));
    if (bytesRead <= 0) break;

    totalRead += static_cast<size_t>(bytesRead);

    for (int i = 0; i < bytesRead; ++i) {
      const char c = buffer[i];

      if (inComment) {
        if (prevStar && c == '/') {
          inComment = false;
          prevStar = false;
          continue;
        }
        prevStar = c == '*';
        continue;
      }

      if (maybeSlash) {
        if (c == '*') {
          inComment = true;
          maybeSlash = false;
          prevStar = false;
          continue;
        }
        handleChar('/');
        maybeSlash = false;
        // fall through to process current char
      }

      if (c == '/') {
        maybeSlash = true;
        continue;
      }

      handleChar(c);
    }
  }

  if (maybeSlash) {
    handleChar('/');
  }

  if (inputTruncated) {
    LOG_ERR("CSS", "CSS input exceeded parser buffer; cache will remain partial");
  }
  const bool incompleteInput = bodyDepth > 0 || inAtRule || inComment || !selector.empty();
  LOG_DBG("CSS", "Parsed %zu rules from %zu bytes", ruleCount(), totalRead);
  return ruleGrowthStopped_ || inputTruncated || incompleteInput ? ParseResult::Partial : ParseResult::Complete;
}

// Style resolution

CssStyle CssParser::resolveStyle(std::string_view tagName, std::string_view classAttr) const {
  CssStyle result;

  // 1. Apply element-level style (lowest priority).
  if (const CssStyle* style = findStyle(tagName)) {
    result.applyOver(*style);
  }

  if (classAttr.empty()) return result;

  // TODO: Support combinations of classes (e.g. style on .class1.class2)
  // 2. Apply class styles (medium priority).
  forEachDelimitedToken(classAttr, isCssWhitespace, [&](std::string_view cls) {
    if (const CssStyle* style = findStyle(".", cls)) {
      result.applyOver(*style);
    }
  });

  // TODO: Support combinations of classes (e.g. style on p.class1.class2)
  // 3. Apply element.class styles (higher priority).
  forEachDelimitedToken(classAttr, isCssWhitespace, [&](std::string_view cls) {
    if (const CssStyle* style = findStyle(tagName, ".", cls)) {
      result.applyOver(*style);
    }
  });

  return result;
}

// Inline style parsing (static - doesn't need rule database)

CssStyle CssParser::parseInlineStyle(std::string_view styleValue) { return parseDeclarations(styleValue); }

// Cache serialization

// Cache file name (version is CssParser::CSS_CACHE_VERSION)
constexpr char rulesCache[] = "/css_rules.cache";
constexpr char rulesCacheTmp[] = "/css_rules.cache.tmp";
constexpr char rulesCacheBackup[] = "/css_rules.cache.bak";
constexpr uint8_t CSS_CACHE_FLAG_PARTIAL = 1 << 0;
constexpr uint8_t CSS_CACHE_FLAG_CHECKSUM = 1 << 1;
constexpr uint8_t CSS_CACHE_KNOWN_FLAGS = CSS_CACHE_FLAG_PARTIAL | CSS_CACHE_FLAG_CHECKSUM;

namespace {
uint32_t checksumBytes(uint32_t checksum, const void* data, const size_t length) {
  const auto* bytes = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < length; ++i) {
    checksum ^= bytes[i];
    checksum *= 16777619u;
  }
  return checksum;
}

uint32_t cacheHeaderChecksum(const uint8_t flags, const uint16_t count, const uint32_t bodyBytes,
                             const uint32_t bodyChecksum) {
  const uint8_t header[] = {CssParser::CSS_CACHE_VERSION, flags, static_cast<uint8_t>(count),
                           static_cast<uint8_t>(count >> 8)};
  uint32_t checksum = checksumBytes(2166136261u, header, sizeof(header));
  checksum = checksumBytes(checksum, &bodyBytes, sizeof(bodyBytes));
  return checksumBytes(checksum, &bodyChecksum, sizeof(bodyChecksum));
}

class CacheInput {
  HalFile& file;
  uint8_t buffer[512];
  size_t position = 0, buffered = 0, remaining;
  uint32_t checksum = 2166136261u;
  size_t consumed = 0;
 public:
  size_t bytes = 0, reads = 0;
  explicit CacheInput(HalFile& source) : file(source), remaining(source.available()) {}
  bool read(void* destination, const size_t length, const bool hash = true) {
    if (length > remaining + buffered - position) return false;
    auto* target = static_cast<uint8_t*>(destination);
    for (size_t copied = 0; copied < length;) {
      if (position == buffered) {
        buffered = std::min(remaining, sizeof(buffer));
        const int received = file.read(buffer, buffered);
        ++reads;
        if (received > 0) bytes += static_cast<size_t>(received);
        if (received != static_cast<int>(buffered)) return false;
        remaining -= buffered;
        position = 0;
      }
      const size_t count = std::min(length - copied, buffered - position);
      memcpy(target + copied, buffer + position, count);
      position += count;
      copied += count;
    }
    consumed += length;
    if (hash) checksum = checksumBytes(checksum, destination, length);
    return true;
  }
  template <typename T> bool pod(T& value) { return read(&value, sizeof(value)); }
  bool finish(const uint8_t flags, const uint16_t count) {
    if (flags & CSS_CACHE_FLAG_CHECKSUM) {
      const size_t bodyBytes = consumed;
      uint32_t footer[3] = {};
      if (!read(footer, sizeof(footer), false) || footer[0] != bodyBytes || footer[1] != checksum ||
          footer[2] != cacheHeaderChecksum(flags, count, footer[0], footer[1])) return false;
    }
    return remaining == 0 && position == buffered;
  }
};

bool readCacheHeader(CacheInput& input, uint8_t& flags, uint16_t& count) {
  uint8_t version = 0;
  return input.pod(version) && version == CssParser::CSS_CACHE_VERSION && input.pod(flags) &&
         (flags & ~CSS_CACHE_KNOWN_FLAGS) == 0 && input.pod(count) && count <= MAX_RULES;
}

#ifdef TENOR_PRESS_PROBE
struct CacheProbe {
  CacheInput& input;
  const char* stage;
  unsigned long started = millis();
  ~CacheProbe() {
    LOG_INF("CSS", "CSS_CACHE stage=%s bytes=%u reads=%u ms=%lu free=%u largest=%u", stage,
            static_cast<unsigned>(input.bytes), static_cast<unsigned>(input.reads), millis() - started,
            static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
  }
};
#endif
}  // namespace

bool CssParser::hasCache() const { return Storage.exists((cachePath + rulesCache).c_str()); }

bool CssParser::restoreCacheBackupIfNeeded() const {
  if (cachePath.empty()) {
    return false;
  }

  const std::string finalPath = cachePath + rulesCache;
  if (Storage.exists(finalPath.c_str())) {
    return true;
  }

  const std::string backupPath = cachePath + rulesCacheBackup;
  if (!Storage.exists(backupPath.c_str())) {
    return false;
  }

  if (!Storage.rename(backupPath.c_str(), finalPath.c_str())) {
    LOG_ERR("CSS", "Failed to restore CSS cache backup");
    return false;
  }

  LOG_DBG("CSS", "Restored CSS cache backup after interrupted replacement");
  return true;
}

void CssParser::deleteCache() const {
  cacheNeedsRebuild_ = true;
  if (hasCache()) Storage.remove((cachePath + rulesCache).c_str());
  Storage.remove((cachePath + rulesCacheTmp).c_str());
  Storage.remove((cachePath + rulesCacheBackup).c_str());
}

CssParser::CacheStatus CssParser::inspectCache() const {
  if (cachePath.empty() || (!hasCache() && !restoreCacheBackupIfNeeded())) {
    return CacheStatus::Missing;
  }

  HalFile file;
  if (!Storage.openFileForRead("CSS", cachePath + rulesCache, file)) {
    return CacheStatus::Invalid;
  }

#ifdef TENOR_PRESS_PROBE
  const unsigned long started = millis();
#endif
  uint8_t header[4] = {};
  const size_t fileBytes = file.available();
  if (file.read(header, sizeof(header)) != sizeof(header) || header[0] != CSS_CACHE_VERSION ||
      (header[1] & ~CSS_CACHE_KNOWN_FLAGS) != 0) return CacheStatus::Invalid;
  uint8_t flags = header[1];
  uint16_t ruleCount = static_cast<uint16_t>(header[2] | (header[3] << 8));
  if (ruleCount > MAX_RULES) return CacheStatus::Invalid;
  if (flags & CSS_CACHE_FLAG_CHECKSUM) {
    uint32_t footer[3] = {};
    if (fileBytes < sizeof(header) + sizeof(footer) || !file.seek(fileBytes - sizeof(footer)) ||
        file.read(footer, sizeof(footer)) != sizeof(footer) || footer[0] != fileBytes - sizeof(footer) ||
        footer[0] < sizeof(header) + ruleCount * (sizeof(uint16_t) + 1 + STYLE_WIRE_BYTES) ||
        footer[0] > sizeof(header) + ruleCount * (sizeof(uint16_t) + STYLE_WIRE_BYTES) + SELECTOR_POOL_CAP ||
        footer[2] != cacheHeaderChecksum(flags, ruleCount, footer[0], footer[1])) return CacheStatus::Invalid;
#ifdef TENOR_PRESS_PROBE
    LOG_INF("CSS", "CSS_CACHE stage=inspect bytes=16 reads=2 ms=%lu free=%u largest=%u", millis() - started,
            static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
#endif
    return flags & CSS_CACHE_FLAG_PARTIAL ? CacheStatus::Partial : CacheStatus::Complete;
  }
  // Legacy v12 caches have no checksum footer; validate their records without retaining rules.
  if (!file.seek(0)) return CacheStatus::Invalid;
  CacheInput input(file);
#ifdef TENOR_PRESS_PROBE
  CacheProbe probe{input, "inspect_legacy"};
#endif
  if (!readCacheHeader(input, flags, ruleCount)) return CacheStatus::Invalid;
  size_t selectorBytes = 0, previousLength = 0;
  char selector[MAX_SELECTOR_LENGTH], previous[MAX_SELECTOR_LENGTH];
  for (uint16_t i = 0; i < ruleCount; ++i) {
    uint16_t length = 0;
    if (!input.pod(length) || length == 0 || length > MAX_SELECTOR_LENGTH) return CacheStatus::Invalid;
    selectorBytes += length;
    uint8_t wire[STYLE_WIRE_BYTES];
    CssStyle style;
    if (selectorBytes > SELECTOR_POOL_CAP || !input.read(selector, length) || !input.read(wire, sizeof(wire)) ||
        !decodeStyleWire(wire, style)) return CacheStatus::Invalid;
    if (i && std::string_view(previous, previousLength) >= std::string_view(selector, length)) return CacheStatus::Invalid;
    memcpy(previous, selector, length);
    previousLength = length;
  }
  if (!input.finish(flags, ruleCount)) return CacheStatus::Invalid;
  return flags & CSS_CACHE_FLAG_PARTIAL ? CacheStatus::Partial : CacheStatus::Complete;
}

bool CssParser::saveToCache(const bool complete) const {
  if (cachePath.empty()) {
    return false;
  }

  const std::string finalPath = cachePath + rulesCache;
  const std::string tmpPath = cachePath + rulesCacheTmp;
  const std::string backupPath = cachePath + rulesCacheBackup;

  Storage.remove(tmpPath.c_str());

  HalFile file;
  if (!Storage.openFileForWrite("CSS", tmpPath, file)) {
    return false;
  }

  bool writeOk = true;
  uint32_t checksum = 2166136261u, bodyBytes = 0;
  const auto writeBytes = [&file, &writeOk, &checksum, &bodyBytes](const void* data, const size_t size) {
    if (writeOk && size > 0 && file.write(data, size) != size) {
      writeOk = false;
    }
    if (writeOk) {
      checksum = checksumBytes(checksum, data, size);
      bodyBytes += size;
    }
  };
  const auto writeByte = [&writeBytes](const uint8_t value) { writeBytes(&value, sizeof(value)); };

  writeByte(CssParser::CSS_CACHE_VERSION);

  // A partial cache can style the current low-memory session, but the next
  // EPUB load must retry the source stylesheets instead of trusting it.
  const uint8_t flags = CSS_CACHE_FLAG_CHECKSUM | (complete ? 0 : CSS_CACHE_FLAG_PARTIAL);
  writeByte(flags);

  // Write rule count
  const uint16_t ruleCount = entryCount_;
  writeBytes(&ruleCount, sizeof(ruleCount));

  // Write each rule: selector string + CssStyle fields
  for (uint16_t i = 0; i < entryCount_; ++i) {
    const std::string_view selector = selectorAt(i);
    // Write selector string (length-prefixed)
    const auto selectorLen = static_cast<uint16_t>(selector.size());
    writeBytes(&selectorLen, sizeof(selectorLen));
    writeBytes(selector.data(), selectorLen);

    uint8_t styleWire[STYLE_WIRE_BYTES];
    encodeStyleWire(stylePool_[entries_[i].styleIndex], styleWire);
    writeBytes(styleWire, sizeof(styleWire));
    if (!writeOk) break;
  }

  const uint32_t footer[] = {bodyBytes, checksum, cacheHeaderChecksum(flags, ruleCount, bodyBytes, checksum)};
  writeOk = writeOk && file.write(footer, sizeof(footer)) == sizeof(footer) && file.sync();
  const bool closed = file.close();
  if (!writeOk || !closed) {
    LOG_ERR("CSS", "Failed to write temporary CSS cache");
    Storage.remove(tmpPath.c_str());
    return false;
  }

  const bool hadExistingCache = Storage.exists(finalPath.c_str());
  if (hadExistingCache) {
    Storage.remove(backupPath.c_str());
    if (!Storage.rename(finalPath.c_str(), backupPath.c_str())) {
      LOG_ERR("CSS", "Failed to back up existing CSS cache");
      Storage.remove(tmpPath.c_str());
      return false;
    }
  }

  if (!Storage.rename(tmpPath.c_str(), finalPath.c_str())) {
    LOG_ERR("CSS", "Failed to promote temporary CSS cache");
    Storage.remove(tmpPath.c_str());
    if (Storage.exists(backupPath.c_str()) && !Storage.rename(backupPath.c_str(), finalPath.c_str())) {
      LOG_ERR("CSS", "Failed to restore previous CSS cache");
    }
    return false;
  }

  Storage.remove(backupPath.c_str());

  cacheNeedsRebuild_ = false;
  LOG_DBG("CSS", "Saved %u rules to %s cache", ruleCount, complete ? "complete" : "partial");
  return true;
}

CssParser::CacheLoadResult CssParser::loadFromCache() {
  if (cachePath.empty()) {
    return CacheLoadResult::Invalid;
  }

  HalFile file;
  if (!Storage.openFileForRead("CSS", cachePath + rulesCache, file)) {
    if (!restoreCacheBackupIfNeeded() || !Storage.openFileForRead("CSS", cachePath + rulesCache, file)) {
      return CacheLoadResult::Invalid;
    }
  }

  // Clear existing rules
  clear();

  CacheInput input(file);
#ifdef TENOR_PRESS_PROBE
  CacheProbe probe{input, "hydrate"};
#endif
  uint8_t flags = 0;
  uint16_t ruleCount = 0;
  if (!readCacheHeader(input, flags, ruleCount)) return CacheLoadResult::Invalid;

  const PoolResult entryCapacityResult = ensureCapacity(ruleCount, 0, 0);
  if (entryCapacityResult == PoolResult::OutOfMemory) {
    clear();
    return CacheLoadResult::LowMemory;
  }
  if (entryCapacityResult == PoolResult::Limit) {
    clear();
    return CacheLoadResult::Invalid;
  }

  auto selectorBuffer = ruleCount > 0 ? makeUniqueNoThrow<char[]>(MAX_SELECTOR_LENGTH) : nullptr;
  if (ruleCount > 0 && !selectorBuffer) {
    clear();
    return CacheLoadResult::LowMemory;
  }

  // Read each rule
  for (uint16_t i = 0; i < ruleCount; ++i) {
    // Read selector string
    uint16_t selectorLen = 0;
    if (!input.pod(selectorLen)) {
      clear();
      return CacheLoadResult::Invalid;
    }

    if (selectorLen == 0 || selectorLen > MAX_SELECTOR_LENGTH) {
      LOG_DBG("CSS", "Invalid selector length in cache: %u", selectorLen);
      clear();
      return CacheLoadResult::Invalid;
    }

    if (!input.read(selectorBuffer.get(), selectorLen)) {
      clear();
      return CacheLoadResult::Invalid;
    }

    uint8_t styleWire[STYLE_WIRE_BYTES];
    if (!input.read(styleWire, sizeof(styleWire))) {
      clear();
      return CacheLoadResult::Invalid;
    }

    CssStyle style;
    if (!decodeStyleWire(styleWire, style)) {
      clear();
      return CacheLoadResult::Invalid;
    }

    const RuleInsertResult insertResult = insertOrMerge(std::string_view(selectorBuffer.get(), selectorLen), style);
    if (insertResult == RuleInsertResult::OutOfMemory) {
      clear();
      return CacheLoadResult::LowMemory;
    }
    if (insertResult == RuleInsertResult::Limit) {
      clear();
      return CacheLoadResult::Invalid;
    }
    if (insertResult == RuleInsertResult::Merged) {
      LOG_DBG("CSS", "Duplicate selector in CSS cache");
      clear();
      return CacheLoadResult::Invalid;
    }
  }

  if (!input.finish(flags, ruleCount)) {
    clear();
    return CacheLoadResult::Invalid;
  }

  const bool partial = (flags & CSS_CACHE_FLAG_PARTIAL) != 0;
  LOG_DBG("CSS", "Loaded %u rules from %s cache", ruleCount, partial ? "partial" : "complete");
  cacheNeedsRebuild_ = false;
  return CacheLoadResult::Complete;
}
