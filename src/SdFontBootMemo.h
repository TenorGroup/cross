#pragma once

#include <SdCardFontRegistry.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

// The SD reader family the last boot loaded, kept in RTC memory for the next wake from deep
// sleep. With it the wake loads that one family straight away instead of walking every family
// folder on the card first (about 300 ms with 31 families on the X3); the full catalog is read
// on first use. The memo lists file names, so the load itself checks it: a file that no longer
// opens sends the boot back to the full scan. Only a deep-sleep wake reads it, and a family of
// more than one file stem or more than MAX_SIZES sizes is not kept (it keeps the full scan).
namespace sdfontmemo {

constexpr uint32_t MAGIC = 0x53464D31u;
constexpr size_t MAX_SIZES = 24;

struct Memo {
  uint32_t magic;
  uint32_t sum;
  char name[32];
  char stem[40];
  uint8_t hiddenRoot;
  uint8_t count;
  uint8_t sizes[MAX_SIZES];
};

// FNV-1a over the fields from name to the last size: they sit back to back, so padding never
// enters the sum.
inline uint32_t checksum(const Memo& memo) {
  const auto* bytes = reinterpret_cast<const uint8_t*>(&memo);
  uint32_t hash = 2166136261u;
  for (size_t i = offsetof(Memo, name); i < offsetof(Memo, sizes) + MAX_SIZES; ++i) {
    hash ^= bytes[i];
    hash *= 16777619u;
  }
  return hash;
}

// False, with the memo cleared, for a family the memo cannot describe.
inline bool save(const SdCardFontFamilyInfo& family, Memo& memo) {
  memo = Memo{};
  if (family.stems.size() != 1 || family.files.empty() || family.files.size() > MAX_SIZES ||
      family.name.size() >= sizeof(memo.name) || family.stems[0].size() >= sizeof(memo.stem)) {
    return false;
  }
  for (size_t i = 0; i < family.files.size(); ++i) {
    if (family.files[i].style != 0) return false;  // magic still unset: no memo
    memo.sizes[i] = family.files[i].pointSize;
  }
  memcpy(memo.name, family.name.c_str(), family.name.size());
  memcpy(memo.stem, family.stems[0].c_str(), family.stems[0].size());
  memo.hiddenRoot = family.hiddenRoot ? 1 : 0;
  memo.count = static_cast<uint8_t>(family.files.size());
  memo.sum = checksum(memo);
  memo.magic = MAGIC;
  return true;
}

// Fills `out`, an empty family, with the one named `wanted` from the memo; false when this boot
// must scan.
inline bool restore(const Memo& memo, const bool deepSleepWake, const char* wanted, SdCardFontFamilyInfo& out) {
  if (!deepSleepWake || memo.magic != MAGIC || memo.sum != checksum(memo) || memo.count == 0 ||
      memo.count > MAX_SIZES || memo.name[sizeof(memo.name) - 1] != '\0' || memo.stem[sizeof(memo.stem) - 1] != '\0' ||
      strcmp(memo.name, wanted) != 0) {
    return false;
  }
  out.name = memo.name;
  out.stems.push_back(out.name);  // one entry, then its text: reuses the registry's own insert
  out.stems.back() = memo.stem;
  out.hiddenRoot = memo.hiddenRoot != 0;
  for (uint8_t i = 0; i < memo.count; ++i) out.files.push_back({memo.sizes[i], 0, 0});
  return true;
}

}  // namespace sdfontmemo
