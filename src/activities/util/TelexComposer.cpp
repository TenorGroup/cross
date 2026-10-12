#include "TelexComposer.h"

#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
#include <cstdint>
#include <cstring>

namespace telex {
namespace {
constexpr size_t MAX_WORD = 32;
// Rows: a, breve a, circumflex a, e, circumflex e, i, o, circumflex o,
// horn o, u, horn u, y. Columns: plain, acute, grave, hook, tilde, dot.
constexpr uint16_t LETTERS[24][6] = {
    {0x61,0xE1,0xE0,0x1EA3,0xE3,0x1EA1}, {0x103,0x1EAF,0x1EB1,0x1EB3,0x1EB5,0x1EB7},
    {0xE2,0x1EA5,0x1EA7,0x1EA9,0x1EAB,0x1EAD}, {0x65,0xE9,0xE8,0x1EBB,0x1EBD,0x1EB9},
    {0xEA,0x1EBF,0x1EC1,0x1EC3,0x1EC5,0x1EC7}, {0x69,0xED,0xEC,0x1EC9,0x129,0x1ECB},
    {0x6F,0xF3,0xF2,0x1ECF,0xF5,0x1ECD}, {0xF4,0x1ED1,0x1ED3,0x1ED5,0x1ED7,0x1ED9},
    {0x1A1,0x1EDB,0x1EDD,0x1EDF,0x1EE1,0x1EE3}, {0x75,0xFA,0xF9,0x1EE7,0x169,0x1EE5},
    {0x1B0,0x1EE9,0x1EEB,0x1EED,0x1EEF,0x1EF1}, {0x79,0xFD,0x1EF3,0x1EF7,0x1EF9,0x1EF5},
    {0x41,0xC1,0xC0,0x1EA2,0xC3,0x1EA0}, {0x102,0x1EAE,0x1EB0,0x1EB2,0x1EB4,0x1EB6},
    {0xC2,0x1EA4,0x1EA6,0x1EA8,0x1EAA,0x1EAC}, {0x45,0xC9,0xC8,0x1EBA,0x1EBC,0x1EB8},
    {0xCA,0x1EBE,0x1EC0,0x1EC2,0x1EC4,0x1EC6}, {0x49,0xCD,0xCC,0x1EC8,0x128,0x1ECA},
    {0x4F,0xD3,0xD2,0x1ECE,0xD5,0x1ECC}, {0xD4,0x1ED0,0x1ED2,0x1ED4,0x1ED6,0x1ED8},
    {0x1A0,0x1EDA,0x1EDC,0x1EDE,0x1EE0,0x1EE2}, {0x55,0xDA,0xD9,0x1EE6,0x168,0x1EE4},
    {0x1AF,0x1EE8,0x1EEA,0x1EEC,0x1EEE,0x1EF0}, {0x59,0xDD,0x1EF2,0x1EF6,0x1EF8,0x1EF4},
};
struct Letter {
  uint16_t code;
  int8_t row = -1;
  uint8_t tone = 0;
  explicit Letter(uint16_t cp = 0) : code(cp) {
    for (int r = 0; r < 24; ++r)
      for (int t = 0; t < 6; ++t)
        if (LETTERS[r][t] == cp) { row = r; tone = t; return; }
  }
  int shape() const { return row < 0 ? -1 : row % 12; }
  void reshape(int r) { row = r + (row >= 12 ? 12 : 0); }
  uint16_t value() const { return row < 0 ? code : LETTERS[row][tone]; }
};
char lower(uint16_t c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c < 128 ? c : 0; }
bool asciiLetter(char c) { const char k = lower(c); return k >= 'a' && k <= 'z'; }
bool wordLetter(const Letter& c) { return c.row >= 0 || asciiLetter(c.code) || c.code == 0x111 || c.code == 0x110; }

int tonePosition(const Letter* word, size_t count) {
  int vowels[MAX_WORD + 1];
  int n = 0;
  for (size_t i = 0; i < count; ++i) {
    if (word[i].row < 0) continue;
    if (i == 1 && count > 2 && word[2].row >= 0 &&
        ((lower(word[0].code) == 'q' && word[i].shape() == 9) ||
         (lower(word[0].code) == 'g' && word[i].shape() == 5))) continue;
    vowels[n++] = static_cast<int>(i);
  }
  if (n == 0) return -1;
  for (int j = n - 1; j >= 0; --j)
    if (word[vowels[j]].shape() == 4 || word[vowels[j]].shape() == 8) return vowels[j];
  for (int j = n - 1; j >= 0; --j) {
    const int shape = word[vowels[j]].shape();
    if (shape == 1 || shape == 2 || shape == 7 || shape == 10) return vowels[j];
  }
  if (n >= 3) return vowels[n - 2];
  if (n == 2) {
    const int first = word[vowels[0]].shape(), second = word[vowels[1]].shape();
    if ((first == 6 && (second == 0 || second == 3)) || (first == 9 && second == 11) ||
        vowels[1] + 1 < static_cast<int>(count)) return vowels[1];
  }
  return vowels[0];
}

void placeTone(Letter* word, size_t count, uint8_t tone) {
  for (size_t i = 0; i < count; ++i) word[i].tone = 0;
  const int index = tonePosition(word, count);
  if (index >= 0) word[index].tone = tone;
}

bool decodePrevious(const std::string& text, size_t end, size_t& start, Letter& letter) {
  start = end - 1;
  while (start > 0 && (static_cast<uint8_t>(text[start]) & 0xC0) == 0x80) --start;
  const auto* p = reinterpret_cast<const uint8_t*>(text.data() + start);
  const size_t n = end - start;
  uint16_t cp;
  if (n == 1 && p[0] < 128) cp = p[0];
  else if (n == 2 && (p[0] & 0xE0) == 0xC0) cp = ((p[0] & 31) << 6) | (p[1] & 63);
  else if (n == 3 && (p[0] & 0xF0) == 0xE0) cp = ((p[0] & 15) << 12) | ((p[1] & 63) << 6) | (p[2] & 63);
  else return false;
  letter = Letter(cp);
  return wordLetter(letter);
}

size_t encode(char* out, uint16_t cp) {
  if (cp < 128) { out[0] = cp; return 1; }
  if (cp < 2048) { out[0] = 0xC0 | (cp >> 6); out[1] = 0x80 | (cp & 63); return 2; }
  out[0] = 0xE0 | (cp >> 12); out[1] = 0x80 | ((cp >> 6) & 63); out[2] = 0x80 | (cp & 63); return 3;
}
}  // namespace

bool insert(State& state, std::string& text, size_t& cursor, char key, size_t maxLength) {
  const size_t standaloneWEnd = state.standaloneWEnd;
  state.standaloneWEnd = 0;
  if (!asciiLetter(key) || cursor > text.size()) return false;
  Letter word[MAX_WORD + 1];
  size_t count = 0, start = cursor;
  while (start > 0) {
    size_t previous;
    Letter letter;
    if (!decodePrevious(text, start, previous, letter)) break;
    if (count == MAX_WORD) return false;
    word[count++] = letter;
    start = previous;
  }
  for (size_t i = 0; i < count / 2; ++i) {
    const Letter tmp = word[i]; word[i] = word[count - 1 - i]; word[count - 1 - i] = tmp;
  }
  uint8_t tone = 0;
  for (size_t i = 0; i < count; ++i) if (word[i].tone) tone = word[i].tone;
  const char k = lower(key);
  constexpr char TONE_KEYS[] = "sfrxj";
  const char* mark = std::strchr(TONE_KEYS, k);
  bool consumed = false;
  const bool escaped = count && std::strchr("sfrxjzw", lower(word[count - 1].code)) && word[count - 1].row < 0;
  if (mark && !escaped && tonePosition(word, count) >= 0) {
    const uint8_t wanted = mark - TONE_KEYS + 1;
    consumed = tone != wanted;
    tone = consumed ? wanted : 0;
  } else if (k == 'z' && tone) {
    tone = 0;
    consumed = true;
  } else if (k == 'd' && count && (lower(word[count - 1].code) == 'd' ||
                                  word[count - 1].code == 0x111 || word[count - 1].code == 0x110)) {
    Letter& last = word[count - 1];
    if (last.code == 0x111 || last.code == 0x110) last.code = last.code == 0x111 ? 'd' : 'D';
    else { last.code = last.code == 'd' ? 0x111 : 0x110; consumed = true; }
  } else if (!escaped && (k == 'a' || k == 'e' || k == 'o')) {
    const int plain = k == 'a' ? 0 : k == 'e' ? 3 : 6;
    const int shaped = k == 'a' ? 2 : k == 'e' ? 4 : 7;
    for (size_t i = count; i > 0; --i) {
      Letter& letter = word[i - 1];
      if (letter.row < 0) continue;
      if (letter.shape() == plain) { letter.reshape(shaped); consumed = true; }
      else if (letter.shape() == shaped) letter.reshape(plain);
      break;
    }
  } else if (!escaped && k == 'w' && count && cursor == standaloneWEnd && word[count - 1].shape() == 10 && !tone) {
    word[count - 1] = Letter(word[count - 1].row >= 12 ? 'W' : 'w');
    consumed = true;
  } else if (!escaped && k == 'w') {
    // uaw/uuw shape the first vowel; the last a/u belongs to the diphthong.
    if (count >= 2 && word[count - 2].shape() == 9 &&
        (word[count - 1].shape() == 0 || word[count - 1].shape() == 9)) {
      word[count - 2].reshape(10);
      consumed = true;
    }
    for (size_t i = count; i > 0 && !consumed; --i) {
      Letter& letter = word[i - 1];
      if (letter.row < 0) continue;
      const int shape = letter.shape();
      if (shape == 0 || shape == 6 || shape == 9) {
        letter.reshape(shape == 0 ? 1 : shape == 6 ? 8 : 10);
        consumed = true;
        if (shape == 6 && i >= 2 && word[i - 2].shape() == 9 &&
            !(i == 3 && lower(word[0].code) == 'q')) {
          const bool openTh = i == count && i == 4 && lower(word[0].code) == 't' && lower(word[1].code) == 'h';
          const bool openH = i == count && i == 3 && lower(word[0].code) == 'h';
          if (!openTh && !openH) word[i - 2].reshape(10);
        }
      } else if (shape == 1 || shape == 8 || shape == 10) {
        letter.reshape(shape == 1 ? 0 : shape == 8 ? 6 : 9);
        if (shape == 8 && i >= 2 && word[i - 2].shape() == 10) word[i - 2].reshape(9);
      }
      break;
    }
    if (tonePosition(word, count) < 0) {
      word[count++] = Letter(key == 'W' ? 0x1AF : 0x1B0);
      state.standaloneWEnd = cursor + 2;
      consumed = true;
    }
  }
  if (!consumed) word[count++] = Letter(static_cast<uint8_t>(key));
  // The open syllables huơ/thuở keep u. A following coda forms hươ/thươ.
  for (size_t i = 1; i + 1 < count; ++i)
    if (word[i - 1].shape() == 9 && word[i].shape() == 8 && word[i + 1].row < 0 &&
        !(i == 2 && lower(word[0].code) == 'q')) word[i - 1].reshape(10);
  placeTone(word, count, tone);
  char output[(MAX_WORD + 1) * 3];
  size_t length = 0;
  for (size_t i = 0; i < count; ++i) length += encode(output + length, word[i].value());
  if (maxLength && text.size() - (cursor - start) + length > maxLength) {
    state.standaloneWEnd = 0;
    return true;
  }
  text.replace(start, cursor - start, output, length);
  cursor = start + length;
  return true;
}
}  // namespace telex
#endif
