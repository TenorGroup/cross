#include "FontPackInstaller.h"

#include <HalStorage.h>
#include <Logging.h>
#include <StreamingJsonParser.h>
#include <mbedtls/sha256.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>

#include "FontInstaller.h"
#include "ReaderInkWeight.h"
#include "util/TaskWatchdog.h"

namespace {
using Result = FontPackInstaller::Result;
constexpr uint32_t MAX_PACK_BYTES = 128 * 1024 * 1024;
constexpr uint16_t MAX_ENTRIES = 128;
std::atomic_flag installing = ATOMIC_FLAG_INIT;

uint16_t u16(const uint8_t* bytes) { return bytes[0] | (static_cast<uint16_t>(bytes[1]) << 8); }
uint32_t u32(const uint8_t* bytes) { return u16(bytes) | (static_cast<uint32_t>(u16(bytes + 2)) << 16); }
uint32_t crcUpdate(uint32_t value, const uint8_t* bytes, size_t count) {
  for (size_t offset = 0; offset < count; ++offset) {
    value ^= bytes[offset];
    for (uint8_t bit = 0; bit < 8; ++bit) value = (value >> 1) ^ ((value & 1) ? 0xedb88320u : 0);
  }
  return value;
}
struct Digest {
  mbedtls_sha256_context state;
  Digest() { mbedtls_sha256_init(&state); }
  ~Digest() { mbedtls_sha256_free(&state); }
  bool start() { return mbedtls_sha256_starts(&state, 0) == 0; }
  bool update(const void* bytes, size_t count) {
    return mbedtls_sha256_update(&state, static_cast<const uint8_t*>(bytes), count) == 0;
  }
  bool finish(char output[65]) {
    uint8_t bytes[32];
    if (mbedtls_sha256_finish(&state, bytes) != 0) return false;
    for (size_t offset = 0; offset < sizeof(bytes); ++offset) std::snprintf(output + offset * 2, 3, "%02x", bytes[offset]);
    return true;
  }
};
bool copyText(char* target, size_t capacity, const char* source, size_t count) {
  if (count >= capacity) return false;
  std::memcpy(target, source, count); target[count] = 0; return true;
}
bool hexHash(const char* text) {
  if (std::strlen(text) != 64) return false;
  for (size_t offset = 0; offset < 64; ++offset)
    if (!((text[offset] >= '0' && text[offset] <= '9') || (text[offset] >= 'a' && text[offset] <= 'f'))) return false;
  return true;
}
bool entryName(const char* family, const char* name, uint8_t* point = nullptr) {
  if (point) *point = 0;
  if (!std::strcmp(name, "pack.json") || !std::strcmp(name, "OFL.txt")) return true;
  if (!FontInstaller::isValidCpfontRelativePath(name)) return false;
  const char* leaf = name;
  if (std::strchr(name, '/')) {
    if (name[7] < '2' || name[7] > '6') return false;
    leaf += 9;
  }
  const size_t familyLength = std::strlen(family);
  if (std::strncmp(leaf, family, familyLength) || leaf[familyLength] != '_') return false;
  const char* digits = leaf + familyLength + 1;
  char* end = nullptr;
  const unsigned long size = std::strtoul(digits, &end, 10);
  if (digits[0] < '1' || digits[0] > '9' || end == digits || end - digits > 3 || size > 255 || std::strcmp(end, ".cpfont")) return false;
  if (point) *point = static_cast<uint8_t>(size);
  return true;
}
struct Item { char path[88] = {}; char sha[65] = {}; char layout[65] = {}; uint32_t size = 0; uint8_t fields = 0; };
struct Metadata {
  const char* family = nullptr;
  const char* wanted = nullptr;
  const char* wantedSha = nullptr;
  const char* wantedLayout = nullptr;
  uint32_t wantedSize = 0;
  uint8_t point = 0;
  unsigned depth = 0, entriesDepth = 0, levelsDepth = 0, signaturesDepth = 0;
  unsigned itemDepth = 0, levelDepth = 0, roots = 0, matches = 0, entryCount = 0;
  uint8_t rootFields = 0, levels = 0, levelFields = 0;
  uint32_t level = 0, strength = 0;
  char key[64] = {}, rootLayout[65] = {}, directory[16] = {};
  Item item;
  bool error = false, closed = false;
  static Metadata& get(void* context) { return *static_cast<Metadata*>(context); }
  static void keyValue(void* context, const char* value, size_t count) {
    auto& state = get(context);
    if (!copyText(state.key, sizeof(state.key), value, count)) state.key[0] = 0;
  }
  static void objectStart(void* context) {
    auto& state = get(context);
    if (state.depth == 0) ++state.roots;
    if (state.depth == 1 && !std::strcmp(state.key, "layout_signatures")) state.signaturesDepth = 2;
    ++state.depth;
    if (state.entriesDepth && state.depth == state.entriesDepth + 1) { state.item = {}; state.itemDepth = state.depth; }
    if (state.levelsDepth && state.depth == state.levelsDepth + 1) { state.levelFields = 0; state.directory[0] = 0; state.levelDepth = state.depth; }
    state.key[0] = 0;
  }
  static void objectEnd(void* context) {
    auto& state = get(context);
    if (!state.depth) { state.error = true; return; }
    if (state.depth == state.itemDepth) {
      ++state.entryCount;
      if (state.item.fields != 7 || !entryName(state.family, state.item.path) || !hexHash(state.item.sha) || !std::strcmp(state.item.path, "pack.json")) state.error = true;
      if (state.wanted && !std::strcmp(state.wanted, state.item.path)) {
        ++state.matches;
        if (state.item.size != state.wantedSize || std::strcmp(state.item.sha, state.wantedSha)) state.error = true;
        if (state.point && (!hexHash(state.item.layout) || std::strcmp(state.item.layout, state.wantedLayout))) state.error = true;
      }
      state.itemDepth = 0;
    }
    if (state.depth == state.levelDepth) {
      char expected[16] = {};
      if (state.level) std::snprintf(expected, sizeof(expected), "weight-%u", static_cast<unsigned>(state.level + 1));
      if (state.levelFields != 7 || state.level > 5 || state.strength != static_cast<uint32_t>(readerInk::outlineStrength(state.level)) || std::strcmp(expected, state.directory) || (state.levels & (1u << std::min<uint32_t>(state.level, 7)))) state.error = true;
      if (state.level <= 5) state.levels |= static_cast<uint8_t>(1u << state.level);
      state.levelDepth = 0;
    }
    if (state.depth == state.signaturesDepth) state.signaturesDepth = 0;
    --state.depth;
    if (!state.depth) state.closed = true;
    state.key[0] = 0;
  }
  static void arrayStart(void* context) {
    auto& state = get(context);
    if (state.depth == 1 && !std::strcmp(state.key, "entries")) state.entriesDepth = 2;
    if (state.depth == 1 && !std::strcmp(state.key, "levels")) state.levelsDepth = 2;
    ++state.depth; state.key[0] = 0;
  }
  static void arrayEnd(void* context) {
    auto& state = get(context);
    if (!state.depth) { state.error = true; return; }
    if (state.depth == state.entriesDepth) state.entriesDepth = 0;
    if (state.depth == state.levelsDepth) state.levelsDepth = 0;
    --state.depth; state.key[0] = 0;
  }
  static void stringValue(void* context, const char* value, size_t count) {
    auto& state = get(context);
    if (state.closed) state.error = true;
    if (state.depth == 1 && (!std::strcmp(state.key, "family") || !std::strcmp(state.key, "recipe"))) {
      const uint8_t bit = !std::strcmp(state.key, "family") ? 1 : 2;
      const char* expected = bit == 1 ? state.family : "reader-outline-v3-ramp";
      if ((state.rootFields & bit) || std::strlen(expected) != count || std::memcmp(value, expected, count)) state.error = true;
      state.rootFields |= bit;
    } else if (state.depth == state.itemDepth) {
      if (!std::strcmp(state.key, "path")) { if (state.item.fields & 1) state.error = true; state.item.fields |= 1; state.error |= !copyText(state.item.path, sizeof(state.item.path), value, count); }
      if (!std::strcmp(state.key, "sha256")) { if (state.item.fields & 2) state.error = true; state.item.fields |= 2; state.error |= !copyText(state.item.sha, sizeof(state.item.sha), value, count); }
      if (!std::strcmp(state.key, "layout_signature")) state.error |= !copyText(state.item.layout, sizeof(state.item.layout), value, count);
    } else if (state.depth == state.levelDepth && !std::strcmp(state.key, "directory")) {
      if (state.levelFields & 4) state.error = true;
      state.levelFields |= 4; state.error |= !copyText(state.directory, sizeof(state.directory), value, count);
    } else if (state.signaturesDepth && state.depth == state.signaturesDepth) {
      char size[4]; std::snprintf(size, sizeof(size), "%u", state.point);
      if (!std::strcmp(state.key, size)) { if (state.rootLayout[0]) state.error = true; state.error |= !copyText(state.rootLayout, sizeof(state.rootLayout), value, count); }
    }
  }
  static void numberValue(void* context, const char* value, size_t count) {
    auto& state = get(context);
    if (state.closed) state.error = true;
    uint32_t number = 0;
    if (!count || count > 9) { state.error = true; return; }
    for (size_t offset = 0; offset < count; ++offset) {
      if (value[offset] < '0' || value[offset] > '9') {
        if (!std::strcmp(state.key, "size") || !std::strcmp(state.key, "level") || !std::strcmp(state.key, "strength_26_6")) state.error = true;
        return;
      }
      number = number * 10 + static_cast<unsigned>(value[offset] - '0');
    }
    if (state.depth == 1 && (!std::strcmp(state.key, "format") || !std::strcmp(state.key, "cpfont_version"))) {
      const uint8_t bit = !std::strcmp(state.key, "format") ? 4 : 8;
      if ((state.rootFields & bit) || number != (bit == 4 ? 1u : 4u)) state.error = true;
      state.rootFields |= bit;
    } else if (state.depth == state.itemDepth && !std::strcmp(state.key, "size")) {
      if (state.item.fields & 4) state.error = true;
      state.item.fields |= 4; state.item.size = number;
    } else if (state.depth == state.levelDepth) {
      if (!std::strcmp(state.key, "level")) { if (state.levelFields & 1) state.error = true; state.levelFields |= 1; state.level = number; }
      if (!std::strcmp(state.key, "strength_26_6")) { if (state.levelFields & 2) state.error = true; state.levelFields |= 2; state.strength = number; }
    }
  }
};
struct ArchiveInfo { uint32_t jsonOffset = 0, jsonSize = 0, jsonCrc = 0; uint64_t payload = 0; uint16_t count = 0; bool license = false; unsigned fonts = 0; };
struct Workspace {
  uint8_t buffer[4096];
  uint8_t toc[4][32];
  char family[32] = {}, destination[64] = {}, staging[80] = {}, backup[80] = {}, path[192] = {}, name[88] = {};
  char sha[65] = {}, layout[65] = {};
  ArchiveInfo info;
  Metadata metadata;
  Digest digest;
};
static_assert(sizeof(Workspace) <= 7168, "font pack workspace exceeds heap budget");

bool readExact(HalFile& file, void* bytes, size_t count) { return file.read(bytes, count) == static_cast<int>(count); }
bool hashRange(HalFile& file, Workspace& work, uint32_t start, uint32_t count) {
  if (!file.seekSet(start)) return false;
  while (count) {
    const size_t chunk = std::min<size_t>(count, sizeof(work.buffer));
    if (!readExact(file, work.buffer, chunk) || !work.digest.update(work.buffer, chunk)) return false;
    count -= chunk; resetTaskWatchdogIfSubscribed();
  }
  return true;
}
bool fontLayout(const char* path, Workspace& work) {
  HalFile file;
  if (!Storage.openFileForRead("FONTPACK", path, file)) return false;
  uint8_t header[32];
  const uint64_t fileSize = file.fileSize64();
  if (!readExact(file, header, sizeof(header)) || std::memcmp(header, "CPFONT\0\0", 8) || u16(header + 8) != 4 || u16(header + 10) > 1 || !header[12] || header[12] > 4) return false;
  const uint8_t styles = header[12];
  if (!readExact(file, work.toc, styles * 32) || !work.digest.start() || !work.digest.update(header, sizeof(header))) return false;
  uint8_t seen = 0;
  for (uint8_t style = 0; style < styles; ++style) {
    uint8_t* toc = work.toc[style];
    const uint8_t role = toc[0];
    const uint32_t intervals = u32(toc + 4), glyphs = u32(toc + 8), start = u32(toc + 24);
    const uint32_t tables = static_cast<uint32_t>(u16(toc + 17) + u16(toc + 19)) * 3 + static_cast<uint32_t>(toc[21]) * toc[22] + toc[23] * 8;
    const uint64_t glyphStart = static_cast<uint64_t>(start) + intervals * 12;
    const uint64_t tableStart = glyphStart + glyphs * 16;
    const uint64_t bitmapStart = tableStart + tables;
    if (role > 3 || (seen & (1u << role)) || !intervals || intervals > 4096 || !glyphs || glyphs > 65536 || u16(toc + 17) > 4096 || u16(toc + 19) > 4096 || start < 32 + styles * 32 || bitmapStart > fileSize) return false;
    seen |= static_cast<uint8_t>(1u << role);
    uint8_t canonical[32]; std::memcpy(canonical, toc, 32); std::memset(canonical + 24, 0, 4);
    if (!work.digest.update(canonical, 32) || !hashRange(file, work, start, intervals * 12) || !hashRange(file, work, static_cast<uint32_t>(tableStart), tables)) return false;
    uint32_t expectedIndex = 0, previousLast = 0;
    for (uint32_t interval = 0; interval < intervals; ++interval) {
      uint8_t bounds[12];
      if (!file.seekSet(start + interval * 12) || !readExact(file, bounds, 12)) return false;
      const uint32_t first = u32(bounds), last = u32(bounds + 4), index = u32(bounds + 8);
      if (first > last || last > 0x10ffff || (interval && first <= previousLast) || index != expectedIndex || last - first + 1 > glyphs - expectedIndex) return false;
      const uint32_t records = last - first + 1;
      if (!file.seekSet(static_cast<size_t>(glyphStart + index * 16))) return false;
      uint32_t consumed = 0;
      while (consumed < records) {
        const size_t count = std::min<size_t>(records - consumed, sizeof(work.buffer) / 16);
        if (!readExact(file, work.buffer, count * 16)) return false;
        for (size_t record = 0; record < count; ++record) {
          const auto* glyph = work.buffer + record * 16;
          const uint32_t bitmapBytes = (static_cast<uint32_t>(glyph[0]) * glyph[1] + (u16(header + 10) ? 3 : 7)) / (u16(header + 10) ? 4 : 8);
          if (u16(glyph + 8) != bitmapBytes || bitmapStart + u32(glyph + 12) + bitmapBytes > fileSize) return false;
          const uint32_t codepoint = first + consumed + record;
          const uint8_t logical[6] = {static_cast<uint8_t>(codepoint), static_cast<uint8_t>(codepoint >> 8), static_cast<uint8_t>(codepoint >> 16), static_cast<uint8_t>(codepoint >> 24), glyph[2], glyph[3]};
          if (!work.digest.update(logical, sizeof(logical))) return false;
        }
        consumed += count; resetTaskWatchdogIfSubscribed();
      }
      expectedIndex += records; previousLast = last;
    }
    if (expectedIndex != glyphs) return false;
  }
  return work.digest.finish(work.layout);
}
bool manifest(HalFile& archive, Workspace& work, const char* wanted = nullptr, uint32_t size = 0, uint8_t point = 0) {
  work.metadata = {};
  auto& state = work.metadata;
  state.family = work.family; state.wanted = wanted; state.wantedSha = work.sha; state.wantedLayout = work.layout; state.wantedSize = size; state.point = point;
  const JsonCallbacks callbacks = {&state, Metadata::keyValue, Metadata::stringValue, Metadata::numberValue, nullptr, nullptr, Metadata::objectStart, Metadata::objectEnd, Metadata::arrayStart, Metadata::arrayEnd};
  StreamingJsonParser parser(callbacks);
  if (!archive.seekSet(work.info.jsonOffset)) return false;
  uint32_t remaining = work.info.jsonSize, checksum = 0xffffffffu;
  while (remaining) {
    const size_t count = std::min<size_t>(remaining, sizeof(work.buffer));
    if (!readExact(archive, work.buffer, count)) return false;
    checksum = crcUpdate(checksum, work.buffer, count);
    for (size_t index = 0; index < count; ++index) {
      const auto value = work.buffer[index];
      if (state.closed && value != ' ' && value != '\t' && value != '\r' && value != '\n') return false;
      parser.feed(reinterpret_cast<const char*>(work.buffer + index), 1);
    }
    remaining -= count; resetTaskWatchdogIfSubscribed();
  }
  return !parser.hasError() && !state.error && state.closed && state.roots == 1 && state.depth == 0 && state.rootFields == 15 && state.levels == 63 && state.entryCount == work.info.count - 1 && ~checksum == work.info.jsonCrc && (!wanted || state.matches == 1) && (!point || (hexHash(state.rootLayout) && !std::strcmp(state.rootLayout, work.layout)));
}
Result walk(HalFile& archive, Workspace& work, bool extract) {
  if (!archive.seekSet(0)) return Result::INVALID_PACK;
  ArchiveInfo info;
  const uint64_t archiveSize = archive.fileSize64();
  if (archiveSize > MAX_PACK_BYTES) return Result::INVALID_PACK;
  uint32_t centralStart = 0;
  uint16_t centralCount = 0;
  while (archive.position() < archiveSize) {
    const uint32_t position = archive.position();
    uint8_t header[46] = {};
    if (!readExact(archive, header, 4)) return Result::INVALID_PACK;
    const uint32_t signature = u32(header);
    if (signature == 0x04034b50u) {
      if (centralStart || !readExact(archive, header + 4, 26) || ++info.count > MAX_ENTRIES) return Result::INVALID_PACK;
      const uint16_t nameSize = u16(header + 26), extra = u16(header + 28), flags = u16(header + 6);
      const uint32_t size = u32(header + 22), checksum = u32(header + 14);
      if (!nameSize || nameSize >= sizeof(work.name) || (flags & ~0x0800u) || u16(header + 8) != 0 || u32(header + 18) != size || size > MAX_PACK_BYTES || !readExact(archive, work.name, nameSize)) return Result::INVALID_PACK;
      work.name[nameSize] = 0;
      if (std::strlen(work.name) != nameSize || !entryName(work.family, work.name)) return Result::INVALID_PACK;
      const uint64_t payload = static_cast<uint64_t>(position) + 30 + nameSize + extra, next = payload + size;
      if (next > archiveSize || !archive.seekSet(payload)) return Result::INVALID_PACK;
      info.payload += size;
      if (!std::strcmp(work.name, "pack.json")) {
        if (info.jsonOffset || size > 256 * 1024 || !size) return Result::INVALID_PACK;
        info.jsonOffset = payload; info.jsonSize = size; info.jsonCrc = checksum;
      } else if (!std::strcmp(work.name, "OFL.txt")) { if (info.license) return Result::INVALID_PACK; info.license = true; }
      else ++info.fonts;
      if (extract) {
        if (std::snprintf(work.path, sizeof(work.path), "%s/%s", work.staging, work.name) >= static_cast<int>(sizeof(work.path)) || Storage.exists(work.path)) return Result::INVALID_PACK;
        char parent[192]; std::strcpy(parent, work.path); *std::strrchr(parent, '/') = 0;
        if (!Storage.exists(parent) && !Storage.mkdir(parent)) return Result::IO_ERROR;
        HalFile output;
        if (!Storage.openFileForWrite("FONTPACK", work.path, output) || !work.digest.start()) return Result::IO_ERROR;
        uint32_t remaining = size, actualCrc = 0xffffffffu;
        while (remaining) {
          const size_t count = std::min<size_t>(remaining, sizeof(work.buffer));
          if (!readExact(archive, work.buffer, count) || output.write(work.buffer, count) != count || !work.digest.update(work.buffer, count)) return Result::IO_ERROR;
          actualCrc = crcUpdate(actualCrc, work.buffer, count);
          remaining -= count; resetTaskWatchdogIfSubscribed();
        }
        if (!output.sync() || !output.close()) return Result::IO_ERROR;
        if (~actualCrc != checksum) return Result::INVALID_PACK;
        if (!work.digest.finish(work.sha)) return Result::IO_ERROR;
        if (std::strcmp(work.name, "pack.json")) {
          uint8_t point = 0; entryName(work.family, work.name, &point);
          if (point && !fontLayout(work.path, work)) return Result::INVALID_PACK;
          if (!manifest(archive, work, work.name, size, point)) return Result::INVALID_PACK;
        }
      }
      if (!archive.seekSet(next)) return Result::INVALID_PACK;
    } else if (signature == 0x02014b50u) {
      if (!centralStart) centralStart = position;
      if (!readExact(archive, header + 4, 42) || ++centralCount > info.count || u16(header + 10) != 0 || (u16(header + 8) & ~0x0800u) || u16(header + 34) != 0 || ((u32(header + 38) >> 16) & 0xf000) == 0xa000) return Result::INVALID_PACK;
      const uint64_t next = static_cast<uint64_t>(position) + 46 + u16(header + 28) + u16(header + 30) + u16(header + 32);
      if (next > archiveSize || !archive.seekSet(next)) return Result::INVALID_PACK;
    } else if (signature == 0x06054b50u) {
      if (!readExact(archive, header + 4, 18) || !centralStart || u16(header + 4) || u16(header + 6) || u16(header + 8) != info.count || u16(header + 10) != info.count || centralCount != info.count || u32(header + 12) != position - centralStart || u32(header + 16) != centralStart || static_cast<uint64_t>(position) + 22 + u16(header + 20) != archiveSize || !info.jsonOffset || !info.license || !info.fonts || info.payload > MAX_PACK_BYTES) return Result::INVALID_PACK;
      if (!extract) work.info = info;
      return Result::OK;
    } else return Result::INVALID_PACK;
  }
  return Result::INVALID_PACK;
}
bool recoverFamily(const char* family) {
  for (const char* root : {"/fonts", "/.fonts"}) {
    char current[64], backup[80], marker[96];
    std::snprintf(current, sizeof(current), "%s/%s", root, family);
    std::snprintf(backup, sizeof(backup), "%s.old", current);
    if (!Storage.exists(backup)) continue;
    if (!Storage.exists(current)) { if (!Storage.rename(backup, current)) return false; }
    else {
      std::snprintf(marker, sizeof(marker), "%s/.pack-ready", current);
      if (!Storage.exists(marker) || !Storage.removeDir(backup)) return false;
    }
  }
  return true;
}
struct BusyGuard { ~BusyGuard() { installing.clear(std::memory_order_release); } };
}

bool FontPackInstaller::isPackFilename(const char* name) {
  if (!name) return false;
  constexpr char suffix[] = ".cpfontpack";
  size_t size = 0;
  while (size <= 42 && name[size]) ++size;
  if (size <= sizeof(suffix) - 1 || size > 31 + sizeof(suffix) - 1 || std::strcmp(name + size - sizeof(suffix) + 1, suffix)) return false;
  char family[32];
  return copyText(family, sizeof(family), name, size - sizeof(suffix) + 1) && FontInstaller::isValidFamilyName(family);
}
size_t FontPackInstaller::workingSetBytes() { return sizeof(Workspace); }
bool FontPackInstaller::recover(const char* family) {
  if (!FontInstaller::isValidFamilyName(family) || installing.test_and_set(std::memory_order_acquire)) return false;
  BusyGuard guard;
  return recoverFamily(family);
}
FontPackInstaller::Result FontPackInstaller::install(const char* path) {
  if (!path || std::strncmp(path, "/fonts/", 7) || !isPackFilename(path + 7)) return Result::INVALID_NAME;
  if (installing.test_and_set(std::memory_order_acquire)) return Result::BUSY;
  BusyGuard guard;
  std::unique_ptr<Workspace> work(new (std::nothrow) Workspace);
  if (!work) return Result::IO_ERROR;
  copyText(work->family, sizeof(work->family), path + 7, std::strlen(path + 7) - 11);
  if (!recoverFamily(work->family)) return Result::IO_ERROR;
  HalFile archive;
  if (!Storage.openFileForRead("FONTPACK", path, archive)) return Result::IO_ERROR;
  auto result = walk(archive, *work, false);
  if (result != Result::OK || !manifest(archive, *work)) return Result::INVALID_PACK;
  uint64_t freeBytes = 0; uint32_t clusterBytes = 0;
  if (!Storage.freeSpace(freeBytes, clusterBytes) || !clusterBytes) return Result::IO_ERROR;
  if (work->info.payload + static_cast<uint64_t>(work->info.count + 9) * clusterBytes > freeBytes) return Result::NO_SPACE;
  char hidden[64]; std::snprintf(hidden, sizeof(hidden), "/.fonts/%s", work->family);
  std::snprintf(work->destination, sizeof(work->destination), "/fonts/%s", work->family);
  if (Storage.exists(hidden)) {
    if (Storage.exists(work->destination)) return Result::INVALID_PACK;
    std::strcpy(work->destination, hidden);
  }
  std::snprintf(work->staging, sizeof(work->staging), "%s/.staging", work->destination);
  std::snprintf(work->backup, sizeof(work->backup), "%s.old", work->destination);
  if (Storage.exists(work->staging) && !Storage.removeDir(work->staging)) return Result::IO_ERROR;
  if (!Storage.mkdir(work->staging)) return Result::IO_ERROR;
  result = walk(archive, *work, true);
  archive.close();
  if (result != Result::OK) { Storage.removeDir(work->staging); return result; }
  std::snprintf(work->path, sizeof(work->path), "%s/.pack-ready", work->staging);
  HalFile marker;
  const uint8_t ready = 1;
  if (!Storage.openFileForWrite("FONTPACK", work->path, marker) || marker.write(&ready, 1) != 1 || !marker.sync() || !marker.close()) return Result::IO_ERROR;
  if (!Storage.rename(work->destination, work->backup)) return Result::IO_ERROR;
  std::snprintf(work->path, sizeof(work->path), "%s/.staging", work->backup);
  if (!Storage.rename(work->path, work->destination)) {
    Storage.rename(work->backup, work->destination);
    return Result::IO_ERROR;
  }
  if (!Storage.removeDir(work->backup) || !Storage.remove(path)) return Result::IO_ERROR;
  LOG_INF("FONTPACK", "Installed %s entries=%u workspace=%u", work->family, work->info.count, static_cast<unsigned>(sizeof(Workspace)));
  return Result::OK;
}
unsigned FontPackInstaller::scan() {
  HalFile directory;
  if (!Storage.openFileForRead("FONTPACK", "/fonts", directory) || !directory.isDirectory()) return 0;
  unsigned installed = 0;
  while (true) {
    HalFile entry = directory.openNextFile();
    if (!entry) break;
    char name[64];
    const bool candidate = !entry.isDirectory() && entry.getName(name, sizeof(name)) && isPackFilename(name);
    entry.close();
    if (!candidate) continue;
    char path[80]; std::snprintf(path, sizeof(path), "/fonts/%s", name);
    const auto result = install(path);
    if (result == Result::OK) ++installed;
    else LOG_ERR("FONTPACK", "Retained %s error=%u", path, static_cast<unsigned>(result));
  }
  return installed;
}
