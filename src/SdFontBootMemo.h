#pragma once

#include <SdCardFontRegistry.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// The SD reader family the last boot loaded, kept in RTC memory for the next wake from deep
// sleep. With it the wake loads that one family straight away instead of walking every family
// folder on the card first (about 300 ms with 31 families on the X3); the full catalog is read
// on first use. The memo lists file names, so the load itself checks it: a file that no longer
// opens sends the boot back to the full scan. Only a deep-sleep wake reads it, and a family of
// more than one file stem or more than MAX_SIZES sizes is not kept (it keeps the full scan).
namespace sdfontmemo {

constexpr uint32_t MAGIC = 0x53464D32u;
constexpr size_t MAX_SIZES = 24;

struct Memo {
  uint32_t magic;
  char name[64];
  char stem[40];
  uint8_t hiddenRoot;
  uint8_t count;
  uint8_t sizes[MAX_SIZES];
};

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
  memo.magic = MAGIC;
  return true;
}

// Fills `out`, an empty family, with the one named `wanted` from the memo; false when this boot
// must scan.
inline bool restore(const Memo& memo, const bool deepSleepWake, const char* wanted, SdCardFontFamilyInfo& out) {
  if (!deepSleepWake || memo.magic != MAGIC || memo.count == 0 ||
      memo.count > MAX_SIZES || memo.name[sizeof(memo.name) - 1] != '\0' || memo.stem[sizeof(memo.stem) - 1] != '\0' ||
      strcmp(memo.name, wanted) != 0) {
    return false;
  }
  out.name = std::string(memo.name);
  out.stems.push_back(std::string(memo.stem));
  out.hiddenRoot = memo.hiddenRoot != 0;
  for (uint8_t i = 0; i < memo.count; ++i) out.files.push_back({memo.sizes[i], 0, 0});
  return true;
}

// The whole catalog the last walk read, kept the same way, so a wake lists every family (text
// settings, font size, reader menu, web settings) without walking the card again. A load cannot
// check a list of families, so this one carries a checksum over everything it holds; it is
// dropped when fonts change in the app (markRegistryDirty) and on every boot that is not a
// deep-sleep wake. A catalog that does not fit CATALOG_BYTES is not kept (the wake walks).
constexpr uint32_t CATALOG_MAGIC = 0x53464331u;
constexpr size_t CATALOG_BYTES = 1024;

struct Catalog {
  uint32_t magic;
  uint32_t check;  // FNV-1a over bytes, families and data[0, bytes)
  uint16_t bytes;
  uint16_t families;
  uint8_t data[CATALOG_BYTES];
};

inline uint32_t catalogCheck(const Catalog& memo) {
  uint32_t hash = 2166136261u;
  const auto mix = [&hash](const uint8_t byte) { hash = (hash ^ byte) * 16777619u; };
  mix(static_cast<uint8_t>(memo.bytes));
  mix(static_cast<uint8_t>(memo.bytes >> 8));
  mix(static_cast<uint8_t>(memo.families));
  mix(static_cast<uint8_t>(memo.families >> 8));
  for (uint16_t i = 0; i < memo.bytes; ++i) mix(memo.data[i]);
  return hash;
}

// Per family: flags (bit 0 hidden root, bit 1 the one stem is the name), name length and name,
// then unless bit 1 the stem count and each stem as length and text, then the file count and per
// file its point size, followed by its stem index when the family has more than one stem.
// False, with the memo cleared, when the catalog does not fit or holds a style this cannot keep.
inline bool saveCatalog(const std::vector<SdCardFontFamilyInfo>& families, Catalog& memo) {
  memo = Catalog{};
  size_t at = 0;
  bool fits = families.size() <= CATALOG_BYTES / 4;
  const auto put = [&](const size_t value) {
    if (value > 255 || at >= CATALOG_BYTES) fits = false;
    if (fits) memo.data[at++] = static_cast<uint8_t>(value);
  };
  const auto putText = [&](const std::string& text) {
    put(text.empty() ? 256 : text.size());
    for (const char c : text) put(static_cast<uint8_t>(c));
  };
  for (const auto& family : families) {
    const bool stemIsName = family.stems.size() == 1 && family.stems[0] == family.name;
    put((family.hiddenRoot ? 1 : 0) | (stemIsName ? 2 : 0));
    putText(family.name);
    if (!stemIsName) {
      put(family.stems.empty() ? 256 : family.stems.size());
      for (const auto& stem : family.stems) putText(stem);
    }
    put(family.files.size());
    for (const auto& file : family.files) {
      put(file.style != 0 || file.stem >= family.stems.size() ? 256 : file.pointSize);
      if (family.stems.size() > 1) put(file.stem);
    }
  }
  if (!fits) {
    memo = Catalog{};
    return false;
  }
  memo.bytes = static_cast<uint16_t>(at);
  memo.families = static_cast<uint16_t>(families.size());
  memo.check = catalogCheck(memo);
  memo.magic = CATALOG_MAGIC;
  return true;
}

// Fills `out`, empty, with the kept catalog; false (and `out` empty) when this boot must walk.
// A family takes at least four bytes, which bounds the allocation before anything is read.
inline bool restoreCatalog(const Catalog& memo, const bool deepSleepWake, std::vector<SdCardFontFamilyInfo>& out) {
  out.clear();
  if (!deepSleepWake || memo.magic != CATALOG_MAGIC || memo.bytes > CATALOG_BYTES ||
      memo.families > memo.bytes / 4 || memo.check != catalogCheck(memo)) {
    return false;
  }
  const uint8_t* at = memo.data;
  const uint8_t* const end = memo.data + memo.bytes;
  bool bad = false;
  const auto get = [&]() -> uint8_t {
    if (at < end) return *at++;
    bad = true;
    return 0;
  };
  const auto getText = [&](std::string& text) {
    const uint8_t length = get();
    if (length == 0 || length > end - at) {
      bad = true;
      return;
    }
    text.assign(reinterpret_cast<const char*>(at), length);
    at += length;
  };
  out.resize(memo.families);
  for (auto& family : out) {
    const uint8_t flags = get();
    getText(family.name);
    family.hiddenRoot = (flags & 1) != 0;
    family.stems.resize((flags & 2) ? 1 : get());
    if (flags & 2) {
      family.stems[0] = family.name;
    } else {
      for (auto& stem : family.stems) getText(stem);
    }
    family.files.resize(get());  // value-initialized: style 0, stem 0
    for (auto& file : family.files) {
      file.pointSize = get();
      if (family.stems.size() > 1) file.stem = get();
      if (file.stem >= family.stems.size()) bad = true;
    }
    if (flags > 3 || family.stems.empty()) bad = true;
    if (bad) break;
  }
  if (bad || at != end) {
    out.clear();
    return false;
  }
  return true;
}

}  // namespace sdfontmemo
