#include "CoverInk.h"

#include <HalStorage.h>
#include <HeapFloor.h>
#include <RecoverableFile.h>

#include <algorithm>
#include <cstring>

#include "TenorInk.h"

namespace coverink {
namespace {

constexpr uint8_t kMagic[4] = {'C', 'I', 'N', 'K'};
constexpr uint8_t kVersion = 1;
constexpr size_t kHead = 12;
constexpr size_t kBytes = kHead + kFrames * 8;

uint16_t le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | p[1] << 8); }
uint32_t le32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | static_cast<uint32_t>(p[3]) << 24; }

// A whole tenor/ink BMP of this frame: the header CoverStream writes, and every row after it.
bool wholeCover(const std::string& path, const int w, const int h, const bool four) {
  auto file = Storage.open(path.c_str());
  if (!file) return false;
  uint8_t head[30];
  const bool read = file.read(head, sizeof(head)) == static_cast<int>(sizeof(head));
  const size_t size = file.size();
  file.close();
  const int stride = four ? (w * 2 + 31) / 32 * 4 : (w + 31) / 32 * 4;
  return read && head[0] == 'B' && head[1] == 'M' && le32(head + 2) == size &&
         static_cast<int32_t>(le32(head + 18)) == w && static_cast<int32_t>(le32(head + 22)) == -h &&
         le16(head + 28) == (four ? 2 : 1) && size == le32(head + 10) + static_cast<size_t>(stride) * h;
}

// A cut between the steps of freeink::replaceFile leaves a backup: keep whichever copy is whole.
template <typename Valid>
bool settle(const std::string& path, Valid valid) {
  const std::string backup = path + ".davbak";
  if (Storage.exists(backup.c_str())) {
    if (valid(path)) {
      if (!Storage.remove(backup.c_str())) return false;
    } else if (Storage.exists(path.c_str()) && !Storage.remove(path.c_str())) {
      return false;
    }
  }
  return freeink::recoverFile(Storage, path.c_str());
}

bool parse(const uint8_t* b, Record& out) {
  if (std::memcmp(b, kMagic, 4) != 0 || b[4] != kVersion || b[6] > kFrames) return false;
  Record r;
  r.kind = static_cast<Kind>(b[5]);
  if (r.kind != Kind::Unknown && r.kind != Kind::Baseline && r.kind != Kind::Progressive && r.kind != Kind::Failed)
    return false;
  r.count = b[6];
  r.width = le16(b + 8);
  r.height = le16(b + 10);
  for (int i = 0; i < r.count; ++i) {
    const uint8_t* f = b + kHead + i * 8;
    r.frame[i] = {le16(f), le16(f + 2), static_cast<uint8_t>(f[4] ? 1 : 0), static_cast<Status>(f[5]), f[6]};
    if (f[5] > static_cast<uint8_t>(Status::GaveUp)) return false;
  }
  out = r;
  return true;
}

bool readFile(const std::string& path, Record& out) {
  auto file = Storage.open(path.c_str());
  if (!file) return false;
  uint8_t b[kBytes];
  const bool whole = file.size() == kBytes && file.read(b, kBytes) == static_cast<int>(kBytes);
  file.close();
  return whole && parse(b, out);
}

}  // namespace

const Frame* Record::find(const int w, const int h, const bool four) const {
  for (int i = 0; i < count; ++i)
    if (frame[i].is(w, h, four)) return &frame[i];
  return nullptr;
}

Frame& Record::at(const int w, const int h, const bool four) {
  for (int i = 0; i < count; ++i)
    if (frame[i].is(w, h, four)) return frame[i];
  if (count == kFrames) {
    for (int i = 1; i < kFrames; ++i) frame[i - 1] = frame[i];
    --count;
  }
  frame[count] = {static_cast<uint16_t>(w), static_cast<uint16_t>(h), static_cast<uint8_t>(four ? 1 : 0),
                  Status::Pending, 0};
  return frame[count++];
}

bool read(const std::string& path, Record& out) {
  out = Record{};
  if (!settle(path, [](const std::string& p) {
        Record r;
        return readFile(p, r);
      }))
    return false;
  return readFile(path, out);
}

bool write(const std::string& path, const Record& record) {
  uint8_t b[kBytes]{};
  std::memcpy(b, kMagic, 4);
  b[4] = kVersion;
  b[5] = static_cast<uint8_t>(record.kind);
  b[6] = record.count;
  b[8] = record.width & 0xFF;
  b[9] = record.width >> 8;
  b[10] = record.height & 0xFF;
  b[11] = record.height >> 8;
  for (int i = 0; i < record.count; ++i) {
    const Frame& f = record.frame[i];
    uint8_t* p = b + kHead + i * 8;
    p[0] = f.width & 0xFF;
    p[1] = f.width >> 8;
    p[2] = f.height & 0xFF;
    p[3] = f.height >> 8;
    p[4] = f.four;
    p[5] = static_cast<uint8_t>(f.status);
    p[6] = f.failures;
  }
  if (!settle(path, [](const std::string& p) {
        Record r;
        return readFile(p, r);
      }))
    return false;
  const std::string staging = path + ".davtmp";
  HalFile file;
  bool ok = Storage.openFileForWrite("INK", staging, file) && file.write(b, kBytes) == kBytes && file.sync();
  ok = file.close() && ok;
  Record check;
  ok = ok && readFile(staging, check) && freeink::replaceFile(Storage, staging.c_str(), path.c_str());
  if (!ok) Storage.remove(staging.c_str());
  return ok;
}

bool restore(const std::string& inkPath) { return freeink::recoverFile(Storage, inkPath.c_str()); }

void dropCards(const std::string& inkPath) {
  for (const char kind : {'e', '0', '1', '2', '3'}) {
    const std::string card = inkPath + ".card" + kind;
    for (const char* tail : {"", ".davbak", ".davtmp"}) {
      const std::string path = card + tail;
      if (Storage.exists(path.c_str())) Storage.remove(path.c_str());
    }
  }
}

Outcome build(const std::string& cacheDir, const std::string& inkPath, const int width, const int height,
              const bool four, const Source& source, const idlework::Stop& stop, const char** why) {
  const auto because = [why](const char* reason, const Outcome outcome) {
    if (why) *why = reason;
    return outcome;
  };
  // Only a tenor/ink cover of this book's cache is ever written.
  const std::string own = cacheDir + "/ink2_";
  if (inkPath.compare(0, own.size(), own) != 0 || inkPath.find('/', own.size()) != std::string::npos)
    return because("not-ink-path", Outcome::Nothing);
  if (!settle(inkPath, [&](const std::string& p) { return wholeCover(p, width, height, four); }))
    return because("settle", Outcome::Later);
  // A temporary cover is not needed: a cover JPEGDEC cannot decode has none (the 1/8 grid fails, or
  // Home's heap gate keeps the old path from running), and those are the ones this step is for.
  const std::string state = statePath(cacheDir);
  Record record;
  read(state, record);
  if (record.kind == Kind::Baseline) return because("baseline", Outcome::Nothing);
  if (record.kind == Kind::Failed) return because("undecodable", Outcome::Nothing);
  if (const Frame* f = record.find(width, height, four); f && f->status != Status::Pending)
    return because(f->status == Status::Sharp ? "sharp" : "given-up", Outcome::Nothing);

  const auto counted = [&] {
    Frame& f = record.at(width, height, four);
    if (++f.failures >= kMaxFailures) f.status = Status::GaveUp;
    write(state, record);
    return Outcome::Failed;
  };
  // A key leaves no trace; running out of time is counted, so a cover too slow for the step ends.
  const auto stopped = [&] { return stop.timedOut ? counted() : Outcome::Stopped; };
  const auto gone = [&] {
    record.kind = Kind::Failed;
    write(state, record);
    return Outcome::Gone;
  };
  const auto passing = [](const progluma::Result r) { return r == progluma::Result::NoHeap ? Outcome::NoHeap : Outcome::Later; };
  // A frame the kernel cannot take never will: given up now.
  if (!tenorink::frameFits(width, height)) {
    record.at(width, height, four).status = Status::GaveUp;
    write(state, record);
    return Outcome::Failed;
  }
  // From here the step takes memory: Home gives its card snapshot back first.
  if (source.release) source.release(source.user);

  if (record.kind == Kind::Unknown || !record.width || !record.height) {
    progluma::Sniffer sniffer;
    const auto r = source.sniff(source.user, sniffer, stop);
    if (r == progluma::Result::Stopped) return stopped();
    if (r == progluma::Result::Unsupported) {
      record.kind = Kind::Baseline;  // not a JPEG: never rebuilt
      write(state, record);
      return because("not-jpeg", Outcome::Nothing);
    }
    if (r != progluma::Result::Done) return because("sniff", passing(r));
    if (sniffer.kind() == progluma::Kind::Baseline) {
      record.kind = Kind::Baseline;
      write(state, record);
      return because("baseline", Outcome::Nothing);
    }
    if (sniffer.kind() != progluma::Kind::Progressive) return gone();
    record.kind = Kind::Progressive;
    record.width = static_cast<uint16_t>(sniffer.width());
    record.height = static_cast<uint16_t>(sniffer.height());
    if (!write(state, record)) return Outcome::Later;
  }
#if defined(ESP_PLATFORM)
  // Asked before the cover is copied out of the book: a heap that cannot hold the decode spares
  // the copy. The blocks still go through the door one by one below.
  const size_t need = progluma::worstBytes(record.width) + tenorink::streamBytes(width, height, four);
  const size_t biggest = std::max(progluma::biggestBlock(record.width), size_t(width) * 3 * sizeof(int));
  if (!heapfloor::leaves(need, biggest, ESP.getFreeHeap(), ESP.getMaxAllocHeap()))
    return because("forecast", Outcome::NoHeap);
#endif

  const std::string coverPath = cacheDir + "/.cover.jpg";
  const auto copied = source.copy(source.user, coverPath, stop);
  // The copy of the cover goes on every way out of the step.
  const Outcome outcome = copied == progluma::Result::Stopped ? stopped()
                          : copied != progluma::Result::Done  ? because("copy", passing(copied))
                                                              : [&]() -> Outcome {
    HalFile cover;
    if (!Storage.openFileForRead("INK", coverPath, cover)) return Outcome::Later;
    HalFile out;
    struct Feed {
      tenorink::CoverStream* stream;
      const idlework::Stop& stop;
      bool refused;  // the card refused a row
    } feed{nullptr, stop, false};
    progluma::Sink sink;
    sink.user = &feed;
    sink.row = [](void* user, const uint8_t* pixels, const int y) {
      auto& f = *static_cast<Feed*>(user);
      const bool ok = f.stream->push(pixels, y);
      f.refused = !ok;
      return ok;
    };
    sink.stop = [](void* user) { return static_cast<Feed*>(user)->stop(); };
    progluma::Decoder decoder;
    auto r = decoder.open(cover, sink);
    if (r != progluma::Result::Done) {
      cover.close();
      if (r == progluma::Result::Stopped) return stopped();
      if (r == progluma::Result::Corrupt || r == progluma::Result::Unsupported) return gone();
      return passing(r);
    }
    const std::string staging = inkPath + ".davtmp";
    if (!Storage.openFileForWrite("INK", staging, out)) {
      cover.close();
      return counted();
    }
    tenorink::CoverStream stream(decoder.width(), decoder.height(), out, width, height, four);
    feed.stream = &stream;
    bool fits = stream.valid();
  #if defined(ESP_PLATFORM)
    // The blocks under 4 KB skip the door: the heap left with all of them must still be on the floor.
    fits = fits && !heapfloor::crosses(ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  #endif
    if (!fits) {
      cover.close();
      out.close();
      Storage.remove(staging.c_str());
      return Outcome::NoHeap;
    }
    r = decoder.run(sink);
    cover.close();
    if (r == progluma::Result::Stopped) {
      out.close();  // the staging file stays: the next start truncates it
      return stopped();
    }
    if (r == progluma::Result::Corrupt) {
      out.close();
      Storage.remove(staging.c_str());
      return gone();
    }
    const bool synced = r == progluma::Result::Done && stream.complete() && out.sync();
    const bool closed = out.close();
    if (r == progluma::Result::Io && !feed.refused) {
      Storage.remove(staging.c_str());
      return Outcome::Later;  // the cover could not be read: may pass
    }
    // In order: the sharp file whole on the card, in place, the cards drawn from the old one gone,
    // and only then the frame marked sharp. A cut anywhere leaves a readable cover and no mark. A key
    // is heard up to the replacement; after it the step finishes, so cover and mark agree.
    if (synced && closed && stop()) return stopped();
    if (!synced || !closed || !wholeCover(staging, width, height, four)) {
      Storage.remove(staging.c_str());
      return counted();
    }
    if (stop()) return stopped();
    if (!freeink::replaceFile(Storage, staging.c_str(), inkPath.c_str())) {
      Storage.remove(staging.c_str());
      return counted();
    }
    dropCards(inkPath);
    Frame& f = record.at(width, height, four);
    f.status = Status::Sharp;
    f.failures = 0;
    write(state, record);
    return Outcome::Sharp;
  }();
  Storage.remove(coverPath.c_str());
  return outcome;
}

}  // namespace coverink
