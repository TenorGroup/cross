#pragma once
// TCF1, the cable flash channel every X4 Pro release keeps (its USB download mode is fused off, so
// this is the only way to reflash it over the cable). The rules live here with no hardware in them so
// a host test can run them; the caller owns the port, the OTA slot and the clock.
//
// The host sends lines: "TCF1 HELLO" (read only) and "TCF1 BEGIN <size> <sha256> <version>". BEGIN
// waits for the user's yes on the device, or the recovery keys held at power on. Then the device
// answers "TCF1 READY <session> <payload max>" and reads frames, one at a time:
//
//   0  "TCF1"   4  type (1 data, 2 end, 3 abort)   5  0   6  payload length (LE16)
//   8  session (LE32)   12  sequence (LE32)   16  image offset (LE32)
//   20 CRC-32 (zlib) of bytes 0..19 and the payload
//
// Each data frame is answered "TCF1 ACK <seq> <bytes so far>" once written, or "TCF1 NAK <seq> <why>"
// with nothing written: the host sends it again. A frame sent twice (its ACK lost) is answered again,
// never written twice. The bytes go straight into the OTA slot that is not running. END makes that
// slot the boot image only when every byte, the SHA-256 from BEGIN, the chip, the board, the cable
// field (board_tag::dropsCable) and the version are right and the slot verifies (esp_ota_end):
// "TCF1 DONE <size>". Anything else ends with "TCF1 FAIL <why>" and the slot is let go; the running
// image and the boot selection never change. The new image then boots on trial (BootTrial).
#include <mbedtls/sha256.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "network/FirmwareBoardTag.h"
#include "platform/PutFile.h"

namespace cableflash {

constexpr size_t kHeader = 24;
constexpr size_t kPayload = 1024;
constexpr size_t kFrame = kHeader + kPayload;
constexpr uint32_t kSilenceMs = 10000;  // no byte for this long ends the session
constexpr uint32_t kQuietMs = 20;       // after a broken frame, the line is quiet this long before the NAK
constexpr uint32_t kConsentMs = 60000;  // the yes on the device, or the recovery keys, last this long
constexpr int kMaxBadFrames = 16;       // in a row
constexpr uint32_t kMinImage = 64 * 1024;
enum : uint8_t { kData = 1, kEnd = 2, kAbort = 3 };

struct Begin {
  uint32_t size = 0;
  uint8_t sha[32] = {};
  char version[32] = {};
};

enum class PolicyAction : uint8_t { Reject, Push, Replace, ReturnToBook, Finish };

struct PolicyInput {
  bool ending = false;
  bool readerOpen = false;
  bool confirmationOpen = false;
  bool updateOpen = false;
  bool updateIsCable = false;
  bool updateFailed = false;
  const char* book = nullptr;
};

struct PolicyDecision {
  PolicyAction action;
  const char* book;
};

// The only policy for opening a cable session and leaving it. Callers supply screen facts and the
// saved book path, so this rule stays independent of Activity and remains host-testable.
inline PolicyDecision decidePolicy(const PolicyInput& in) {
  if (in.ending) return {in.book && in.book[0] != '\0' ? PolicyAction::ReturnToBook : PolicyAction::Finish, in.book};
  if (in.confirmationOpen) return {PolicyAction::Reject, nullptr};
  if (in.updateOpen) {
    return {in.updateIsCable && in.updateFailed ? PolicyAction::Replace : PolicyAction::Reject, in.book};
  }
  return {in.readerOpen ? PolicyAction::Replace : PolicyAction::Push, in.book};
}

// "<size> <sha256 as 64 hex digits> <version>"; the version is what the user is asked about, and END
// checks that the image says the same.
inline bool parseBegin(const char* args, Begin& out) {
  char hex[65] = {}, version[sizeof(out.version)] = {};
  unsigned long size = 0;
  int used = 0;
  if (std::sscanf(args, "%lu %64s %31s%n", &size, hex, version, &used) != 3 || args[used] != '\0' ||
      std::strlen(hex) != 64 || size < kMinImage || size > 0x7fffffffUL)
    return false;
  for (int i = 0; i < 32; ++i) {
    unsigned byte = 0;
    if (std::sscanf(hex + i * 2, "%2x", &byte) != 1 || !std::strchr("0123456789abcdefABCDEF", hex[i * 2]) ||
        !std::strchr("0123456789abcdefABCDEF", hex[i * 2 + 1]))
      return false;
    out.sha[i] = static_cast<uint8_t>(byte);
  }
  out.size = static_cast<uint32_t>(size);
  std::memcpy(out.version, version, sizeof(out.version));
  return true;
}

enum class Result : uint8_t {
  Ok,
  TooLarge,     // larger than the slot the device really has
  FlashFailed,  // the slot could not be opened, written or chosen
  Silence,      // the cable went quiet (pulled, host gone)
  Aborted,      // the host said ABORT
  Noise,        // broken frames in a row
  BadImage,     // not an ESP app image, short, or the slot did not verify
  WrongChip,
  WrongBoard,
  NoCable,  // an image without the cable channel (board_tag::dropsCable)
  WrongVersion,
  BadSha,
};

inline const char* reason(const Result r) {
  switch (r) {
    case Result::Ok: return "ok";
    case Result::TooLarge: return "too-large";
    case Result::FlashFailed: return "flash";
    case Result::Silence: return "silence";
    case Result::Aborted: return "aborted";
    case Result::Noise: return "noise";
    case Result::BadImage: return "bad-image";
    case Result::WrongChip: return "wrong-chip";
    case Result::WrongBoard: return "wrong-board";
    case Result::NoCable: return "no-cable";
    case Result::WrongVersion: return "wrong-version";
    case Result::BadSha: return "bad-sha";
  }
  return "";
}

// One line to the host, "\n" included, in one write of the port. The X4 Pro's TinyUSB CDC holds 64 B
// and the port's usual timeout is 1 ms (main.cpp), so a longer line written the usual way lost every
// byte after its 64th: a HELLO cut at "state=vali", the next log line glued on. For this one write the
// port waits up to kLineMs for room and holds its lock, so no other task's bytes land inside the line.
// A host that opened the port and does not read costs at most kLineMs; a port with no host costs nothing.
// Port: size_t write(const uint8_t*, size_t), void setTxTimeoutMs(uint32_t). `usualMs` is put back after.
constexpr uint32_t kLineMs = 250;
template <typename Port>
bool sendLine(Port& port, const char* text, const uint32_t usualMs) {
  char line[256];
  const int n = std::snprintf(line, sizeof(line), "%s\n", text);
  if (n <= 0 || static_cast<size_t>(n) >= sizeof(line)) return false;
  port.setTxTimeoutMs(kLineMs);
  const size_t sent = port.write(reinterpret_cast<const uint8_t*>(line), static_cast<size_t>(n));
  port.setTxTimeoutMs(usualMs);
  // Cut short anyway: end it, so the next line still stands alone.
  if (sent > 0 && sent < static_cast<size_t>(n)) port.write(reinterpret_cast<const uint8_t*>("\n"), 1);
  return sent == static_cast<size_t>(n);
}

inline uint32_t le32(const uint8_t* p) {
  return p[0] | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16 | static_cast<uint32_t>(p[3]) << 24;
}

// Port: size_t read(uint8_t* buf, size_t max) returns what is waiting now (0 = nothing yet);
//       void reply(const char* line) sends one line.
// Slot: size_t size() (the slot the device really has), bool begin(size_t), bool write(const uint8_t*, size_t),
//       bool end() (verifies the written image), bool boot() (makes it the next boot), void abort().
// Clock: uint32_t now() in ms. Progress: void(size_t written, size_t total).
// `frame` holds kFrame bytes. Nothing else is allocated here.
template <typename Port, typename Slot, typename Clock, typename Progress>
Result receive(const Begin& begin, const uint32_t session, const uint16_t deviceChip, Port& port, Slot& slot,
               Clock& clock, uint8_t* frame, Progress&& progress) {
  char line[64];
  bool open = false;
  const auto fail = [&](const Result r) {
    if (open) slot.abort();
    std::snprintf(line, sizeof(line), "TCF1 FAIL %s", reason(r));
    port.reply(line);
    return r;
  };
  if (begin.size > slot.size()) return fail(Result::TooLarge);
  if (!slot.begin(begin.size)) return fail(Result::FlashFailed);
  open = true;
  std::snprintf(line, sizeof(line), "TCF1 READY %08lx %u", static_cast<unsigned long>(session),
                static_cast<unsigned>(kPayload));
  port.reply(line);

  uint32_t lastByteAt = clock.now();
  // Exactly n bytes into dst, or false after a silence.
  const auto fill = [&](uint8_t* dst, const size_t n) {
    for (size_t got = 0; got < n;) {
      const size_t k = port.read(dst + got, n - got);
      if (k == 0) {
        if (clock.now() - lastByteAt >= kSilenceMs) return false;
        continue;
      }
      got += k;
      lastByteAt = clock.now();
    }
    return true;
  };
  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts(&sha, 0);
  struct ShaGuard {
    mbedtls_sha256_context& c;
    ~ShaGuard() { mbedtls_sha256_free(&c); }
  } shaGuard{sha};
  board_tag::Scanner tags;
  uint8_t head[24];
  uint32_t at = 0, nextSeq = 0, lastCrc = 0;
  int badFrames = 0;

  while (true) {
    if (!fill(frame, kHeader)) return fail(Result::Silence);
    const uint8_t type = frame[4];
    const size_t len = frame[6] | frame[7] << 8;
    const uint32_t seq = le32(frame + 12), offset = le32(frame + 16);
    const char* why = nullptr;
    if (std::memcmp(frame, "TCF1", 4) != 0 || type < kData || type > kAbort || len > kPayload) {
      // Out of step with the host: let the line go quiet, then ask for the frame again.
      for (uint32_t quietFrom = clock.now();;) {
        uint8_t drop[64];
        if (port.read(drop, sizeof(drop)) > 0) {
          quietFrom = lastByteAt = clock.now();
        } else if (clock.now() - quietFrom >= kQuietMs) {
          break;
        } else if (clock.now() - lastByteAt >= kSilenceMs) {
          return fail(Result::Silence);
        }
      }
      why = "frame";
    } else {
      if (!fill(frame + kHeader, len)) return fail(Result::Silence);
      uint32_t crc = putfile::crcUpdate(0xFFFFFFFFu, frame, 20);
      crc = ~putfile::crcUpdate(crc, frame + kHeader, len);
      if (crc != le32(frame + 20)) {
        why = "crc";
      } else if (le32(frame + 8) != session) {
        why = "session";
      } else if (type == kAbort) {
        return fail(Result::Aborted);
      } else if (type == kData && nextSeq > 0 && seq == nextSeq - 1 && crc == lastCrc) {
        // Its ACK was lost: say it again, write nothing.
        std::snprintf(line, sizeof(line), "TCF1 ACK %lu %lu", static_cast<unsigned long>(seq),
                      static_cast<unsigned long>(at));
        port.reply(line);
        continue;
      } else if (type == kData && (seq != nextSeq || offset != at || len == 0)) {
        why = "order";
      } else if (type == kData) {
        const uint8_t* data = frame + kHeader;
        if (len > begin.size - at) return fail(Result::BadImage);
        for (size_t i = 0; at + i < sizeof(head) && i < len; ++i) head[at + i] = data[i];
        if (at < sizeof(head) && at + len >= sizeof(head)) {
          const uint16_t chip = static_cast<uint16_t>(head[12] | head[13] << 8);
          if (head[0] != 0xE9) return fail(Result::BadImage);
          if (deviceChip == 0xFFFF || chip != deviceChip) return fail(Result::WrongChip);
        }
        tags.feed(data, len);
        if (tags.mismatch()) return fail(Result::WrongBoard);
        mbedtls_sha256_update(&sha, data, len);
        if (!slot.write(data, len)) return fail(Result::FlashFailed);
        at += static_cast<uint32_t>(len);
        lastCrc = crc;
        ++nextSeq;
        badFrames = 0;
        std::snprintf(line, sizeof(line), "TCF1 ACK %lu %lu", static_cast<unsigned long>(seq),
                      static_cast<unsigned long>(at));
        port.reply(line);
        progress(static_cast<size_t>(at), static_cast<size_t>(begin.size));
        continue;
      } else {  // END
        if (at != begin.size) return fail(Result::BadImage);
        uint8_t digest[32];
        mbedtls_sha256_finish(&sha, digest);
        if (std::memcmp(digest, begin.sha, sizeof(digest)) != 0) return fail(Result::BadSha);
        if (!tags.matched()) return fail(Result::WrongBoard);
        if (board_tag::dropsCable(tags)) return fail(Result::NoCable);
        if (!tags.versionIs(begin.version)) return fail(Result::WrongVersion);
        open = false;  // end() lets the slot go whatever it answers
        if (!slot.end()) return fail(Result::BadImage);
        if (!slot.boot()) return fail(Result::FlashFailed);
        std::snprintf(line, sizeof(line), "TCF1 DONE %lu", static_cast<unsigned long>(at));
        port.reply(line);
        return Result::Ok;
      }
    }
    if (++badFrames >= kMaxBadFrames) return fail(Result::Noise);
    std::snprintf(line, sizeof(line), "TCF1 NAK %lu %s", static_cast<unsigned long>(nextSeq), why);
    port.reply(line);
  }
}

}  // namespace cableflash
