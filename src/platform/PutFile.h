#pragma once
// CMD:PUT <path> <size> <crc32>: the file the host sends over the cable, written to the card. The rules
// live here with no hardware in them so a host test can run them: the command line, the CRC (zlib's),
// and the receive loop that fills a small buffer, writes it out in pieces and stops on a wrong sum or
// a silence. The caller owns the port, the card and the clock.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

namespace putfile {

struct Command {
  std::string path;
  uint32_t size = 0;
  uint32_t crc = 0;
};

// "<path> <size> <crc32 as 8 hex digits>". The path has no spaces and starts with '/'.
inline bool parse(const std::string& args, Command& out) {
  const size_t a = args.find(' ');
  if (a == std::string::npos || a == 0 || args[0] != '/') return false;
  const size_t b = args.find(' ', a + 1);
  if (b == std::string::npos || b == a + 1) return false;
  const std::string size = args.substr(a + 1, b - a - 1);
  std::string crc = args.substr(b + 1);
  while (!crc.empty() && (crc.back() == ' ' || crc.back() == '\r')) crc.pop_back();
  if (crc.size() != 8 || size.find_first_not_of("0123456789") != std::string::npos || size.size() > 9) return false;
  if (crc.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) return false;
  out.path = args.substr(0, a);
  out.size = static_cast<uint32_t>(std::strtoul(size.c_str(), nullptr, 10));
  out.crc = static_cast<uint32_t>(std::strtoul(crc.c_str(), nullptr, 16));
  return true;
}

inline uint32_t crcUpdate(uint32_t crc, const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return crc;
}

enum class Result : uint8_t { Ok, Silence, CrcMismatch, WriteFailed };

inline const char* reason(const Result r) {
  switch (r) {
    case Result::Ok: return "ok";
    case Result::Silence: return "no bytes for too long";
    case Result::CrcMismatch: return "crc mismatch";
    case Result::WriteFailed: return "card write failed";
  }
  return "";
}

// Port: size_t read(uint8_t* buf, size_t max) returns what is waiting now (0 = nothing).
// Sink: bool write(const uint8_t* buf, size_t n).
// Clock: uint32_t now() in ms. A silence of silenceMs with bytes still owed is a failure.
template <typename Port, typename Sink, typename Clock>
Result receive(const Command& cmd, Port& port, Sink& sink, Clock& clock, uint8_t* buf, const size_t bufSize,
               const uint32_t silenceMs, uint32_t* crcOut = nullptr) {
  uint32_t crc = 0xFFFFFFFFu, left = cmd.size, lastByteAt = clock.now();
  while (left > 0) {
    const size_t want = left < bufSize ? left : bufSize;
    const size_t n = port.read(buf, want);
    if (n == 0) {
      if (clock.now() - lastByteAt >= silenceMs) return Result::Silence;
      continue;
    }
    lastByteAt = clock.now();
    crc = crcUpdate(crc, buf, n);
    if (!sink.write(buf, n)) return Result::WriteFailed;
    left -= static_cast<uint32_t>(n);
  }
  crc = ~crc;
  if (crcOut) *crcOut = crc;
  return crc == cmd.crc ? Result::Ok : Result::CrcMismatch;
}

}  // namespace putfile
