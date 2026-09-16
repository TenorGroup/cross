#pragma once

#include <cstddef>
#include <cstdint>

// Syntax-only streaming guard for OTA metadata. Storage is bounded and independent
// of input size; the existing SAX parser extracts the release fields.
class StrictJsonValidator {
 public:
  void reset() { *this = StrictJsonValidator(); }
  void feed(const char* data, size_t length) {
    for (size_t i = 0; i < length && !invalid; ++i) consume(data[i]);
  }
  bool complete() const { return !invalid && depth == 0 && states[0] == End && mode == Scan; }

 private:
  enum Expected : uint8_t { Root, KeyOrEnd, Key, Colon, Value, ObjectEnd, ArrayOrEnd, ArrayValue, ArrayEnd, End };
  enum Mode : uint8_t { Scan, String, Escape, Unicode, Number, Literal };
  enum NumberState : uint8_t { Sign, Zero, Integer, Dot, Fraction, Exponent, ExponentSign, ExponentDigits };
  Expected states[33] = {Root};
  uint8_t depth = 0;
  Mode mode = Scan;
  NumberState number = Zero;
  bool invalid = false;
  bool key = false;
  uint8_t unicodeLeft = 0;
  const char* literal = nullptr;

  static bool digit(char c) { return c >= '0' && c <= '9'; }
  static bool whitespace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
  bool startValue() {
    auto& expected = states[depth];
    if (expected == Root)
      expected = End;
    else if (expected == Value)
      expected = ObjectEnd;
    else if (expected == ArrayOrEnd || expected == ArrayValue)
      expected = ArrayEnd;
    else
      invalid = true;
    return !invalid;
  }

  void consume(char c) {
    if (mode == Unicode) {
      if (!digit(c) && !(c >= 'a' && c <= 'f') && !(c >= 'A' && c <= 'F')) invalid = true;
      if (--unicodeLeft == 0) mode = String;
      return;
    }
    if (mode == Escape) {
      mode = String;
      if (c == 'u') {
        unicodeLeft = 4;
        mode = Unicode;
      } else if (c != '"' && c != '\\' && c != '/' && c != 'b' && c != 'f' && c != 'n' && c != 'r' && c != 't') {
        invalid = true;
      }
      return;
    }
    if (mode == String) {
      if (static_cast<unsigned char>(c) < 32) invalid = true;
      if (c == '\\') mode = Escape;
      if (c == '"') {
        mode = Scan;
        if (key) states[depth] = Colon;
      }
      return;
    }
    if (mode == Literal) {
      if (c != *literal++) invalid = true;
      if (!invalid && !*literal) mode = Scan;
      return;
    }
    if (mode == Number) {
      if (digit(c)) {
        if (number == Zero) invalid = true;
        if (number == Sign)
          number = c == '0' ? Zero : Integer;
        else if (number == Dot)
          number = Fraction;
        else if (number == Exponent || number == ExponentSign)
          number = ExponentDigits;
        return;
      }
      if (c == '.' && (number == Zero || number == Integer)) {
        number = Dot;
        return;
      }
      if ((c == 'e' || c == 'E') && (number == Zero || number == Integer || number == Fraction)) {
        number = Exponent;
        return;
      }
      if ((c == '+' || c == '-') && number == Exponent) {
        number = ExponentSign;
        return;
      }
      if (number == Sign || number == Dot || number == Exponent || number == ExponentSign) {
        invalid = true;
        return;
      }
      mode = Scan;
    }
    if (whitespace(c)) return;
    const auto expected = states[depth];
    // The OTA document root is always an object.
    if (expected == Root && c != '{') {
      invalid = true;
      return;
    }
    if (c == '{' || c == '[') {
      if (!startValue()) return;
      if (depth == 32) {
        invalid = true;
        return;
      }
      states[++depth] = c == '{' ? KeyOrEnd : ArrayOrEnd;
    } else if (c == '}' || c == ']') {
      const bool validEnd =
          c == '}' ? expected == KeyOrEnd || expected == ObjectEnd : expected == ArrayOrEnd || expected == ArrayEnd;
      if (!depth || !validEnd)
        invalid = true;
      else
        --depth;
    } else if (c == ':') {
      if (expected != Colon)
        invalid = true;
      else
        states[depth] = Value;
    } else if (c == ',') {
      if (expected == ObjectEnd)
        states[depth] = Key;
      else if (expected == ArrayEnd)
        states[depth] = ArrayValue;
      else
        invalid = true;
    } else if (c == '"') {
      key = expected == Key || expected == KeyOrEnd;
      if (key || startValue()) mode = String;
    } else if (c == '-' || digit(c)) {
      if (!startValue()) return;
      number = c == '-' ? Sign : c == '0' ? Zero : Integer;
      mode = Number;
    } else if (c == 't' || c == 'f' || c == 'n') {
      if (!startValue()) return;
      literal = c == 't' ? "rue" : c == 'f' ? "alse" : "ull";
      mode = Literal;
    } else {
      invalid = true;
    }
  }
};
