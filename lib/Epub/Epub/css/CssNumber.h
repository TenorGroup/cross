#pragma once

#include <cstdint>
#include <cstring>

// A CSS length's number as CssParser::tryInterpretLength cuts it: an optional '-', digits, an
// optional '.' and more digits, one digit at least, nothing after. Gives the float std::from_chars
// gives: the nearest one, ties to even; false for a value past the largest float or one that
// rounds to zero. libstdc++'s float from_chars brought 21 KB of flash for this caller alone.
//
// An approximate double finds the candidate; exact integer comparisons of the decimal value with
// the midpoints around it settle the rounding, so the result does not depend on the double.
namespace cssnumber {

namespace detail {
// Unsigned integer of up to 1.024 bits, little-endian 32-bit limbs.
struct Big {
  static constexpr int LIMBS = 32;
  uint32_t limb[LIMBS] = {};
  int used = 0;

  void mulAdd(const uint32_t m, uint32_t add) {
    for (int i = 0; i < used; i++) {
      const uint64_t v = static_cast<uint64_t>(limb[i]) * m + add;
      limb[i] = static_cast<uint32_t>(v);
      add = static_cast<uint32_t>(v >> 32);
    }
    if (add && used < LIMBS) limb[used++] = add;
  }
  void shiftLeft(int bits) {
    const int words = bits / 32;
    bits %= 32;
    if (words) {
      for (int i = used - 1; i >= 0; i--)
        if (i + words < LIMBS) limb[i + words] = limb[i];
      for (int i = 0; i < words && i < LIMBS; i++) limb[i] = 0;
      used = used + words < LIMBS ? used + words : LIMBS;
    }
    if (bits) {
      uint32_t carry = 0;
      for (int i = 0; i < used; i++) {
        const uint32_t next = limb[i] >> (32 - bits);
        limb[i] = limb[i] << bits | carry;
        carry = next;
      }
      if (carry && used < LIMBS) limb[used++] = carry;
    }
  }
  void pow5(int n) {
    for (; n >= 13; n -= 13) mulAdd(1220703125u, 0);  // 5^13
    static constexpr uint32_t small[13] = {1, 5, 25, 125, 625, 3125, 15625, 78125, 390625, 1953125, 9765625,
                                           48828125, 244140625};
    if (n) mulAdd(small[n], 0);
  }
};

inline int compare(const Big& a, const Big& b) {
  if (a.used != b.used) return a.used < b.used ? -1 : 1;
  for (int i = a.used - 1; i >= 0; i--)
    if (a.limb[i] != b.limb[i]) return a.limb[i] < b.limb[i] ? -1 : 1;
  return 0;
}

// The decimal value digits x 10^exp10 against (2q + 1) x 2^(e2 - 1), the midpoint above q x 2^e2.
inline int compareToMidpoint(const Big& digits, const int exp10, const uint32_t q, const int e2) {
  Big left = digits, right;
  right.limb[0] = 2 * q + 1;
  right.used = 1;
  if (exp10 >= 0) {
    left.pow5(exp10);
  } else {
    right.pow5(-exp10);
  }
  // 10^exp10 = 5^exp10 x 2^exp10; move every power of two to one side.
  const int twos = exp10 - (e2 - 1);
  if (twos >= 0) {
    left.shiftLeft(twos);
  } else {
    right.shiftLeft(-twos);
  }
  return compare(left, right);
}

inline float fromBits(const uint32_t bits) {
  float f;
  std::memcpy(&f, &bits, sizeof(f));
  return f;
}
inline uint32_t toBits(const float f) {
  uint32_t bits;
  std::memcpy(&bits, &f, sizeof(bits));
  return bits;
}
}  // namespace detail

inline bool parse(const char* p, const char* const end, float& out) {
  // Past these many significant digits the rest only says whether the value is above them: a float
  // midpoint has at most 113 significant digits, so a nonzero digit past 120 cannot land on one.
  constexpr int KEEP = 120;
  const bool negative = p < end && *p == '-';
  if (negative) ++p;
  detail::Big digits;
  int kept = 0, pointAt = -1, firstAt = -1, count = 0;
  bool sticky = false, any = false;
  uint64_t lead = 0;  // the first 19 significant digits, for the approximation
  int leadDigits = 0;
  for (; p < end; ++p, ++count) {
    if (*p == '.') {
      if (pointAt >= 0) return false;
      pointAt = count;
      --count;
      continue;
    }
    if (*p < '0' || *p > '9') return false;
    any = true;
    const uint32_t d = static_cast<uint32_t>(*p - '0');
    if (firstAt < 0) {
      if (d == 0) continue;
      firstAt = count;
    }
    if (kept < KEEP) {
      digits.mulAdd(10, d);
      kept++;
      if (leadDigits < 19) {
        lead = lead * 10 + d;
        leadDigits++;
      }
    } else if (d) {
      sticky = true;
    }
  }
  if (!any) return false;
  if (firstAt < 0) {
    out = negative ? -0.0f : 0.0f;
    return true;
  }
  if (pointAt < 0) pointAt = count;
  // The value is digits x 10^exp10; its first significant digit stands for 10^magnitude.
  const int magnitude = pointAt - firstAt - 1;
  if (magnitude >= 39) return false;   // at least 1e39: past the largest float
  if (magnitude <= -47) return false;  // below 1e-46: under half the smallest float
  if (sticky) {
    digits.mulAdd(10, 1);
    kept++;
  }
  const int exp10 = magnitude - kept + 1;

  // The candidate: within a unit or two of the answer.
  double approx = static_cast<double>(lead);
  int scale = magnitude - leadDigits + 1;
  static constexpr double POW10[] = {1e1, 1e2, 1e4, 1e8, 1e16, 1e32, 1e64};
  const bool down = scale < 0;
  if (down) scale = -scale;
  double factor = 1;
  for (int i = 0; scale; i++, scale >>= 1)
    if (scale & 1) factor *= POW10[i];
  approx = down ? approx / factor : approx * factor;
  constexpr uint32_t MAX_BITS = 0x7F7FFFFF;  // the largest finite float
  uint32_t bits = approx >= 3.4028234663852886e38 ? MAX_BITS : detail::toBits(static_cast<float>(approx));

  // Settle on the float whose rounding interval holds the value: the midpoint below it is at or
  // under the value, the midpoint above it at or over.
  const auto midAbove = [&](const uint32_t b) {
    const uint32_t exponent = b >> 23, fraction = b & 0x7FFFFF;
    const uint32_t q = exponent ? fraction | 0x800000 : fraction;
    const int e2 = exponent ? static_cast<int>(exponent) - 150 : -149;
    return detail::compareToMidpoint(digits, exp10, q, e2);
  };
  int above;
  while ((above = midAbove(bits)) > 0) {
    if (bits == MAX_BITS) return false;
    bits++;
  }
  int below = bits ? midAbove(bits - 1) : 1;
  while (below < 0) {
    bits--;
    above = midAbove(bits);
    below = bits ? midAbove(bits - 1) : 1;
  }
  // On a midpoint: the even neighbour.
  if (above == 0 && (bits & 1)) {
    if (bits == MAX_BITS) return false;
    bits++;
  } else if (below == 0 && (bits & 1)) {
    bits--;
  }
  if (bits == 0) return false;  // a nonzero value that rounds to zero
  out = negative ? -detail::fromBits(bits) : detail::fromBits(bits);
  return true;
}

}  // namespace cssnumber
