#pragma once
// One line on the card for every OTA check that fails and every install, so a failure a user
// reports can be read back as numbers: where it stopped, how far it got, the heap and the
// signal at the time, and which timeout ended it. The file keeps the newest lines only.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace ota_log {
constexpr char PATH[] = "/.crosspoint/ota-log.txt";
constexpr size_t MAX_LINES = 20;
constexpr size_t MAX_BYTES = 4096;
constexpr size_t LINE_BYTES = 256;

// How one check or install ended. OtaUpdater fills it; formatFields() prints it.
struct Attempt {
  bool ok = false;
  const char* step = "none";  // ntp, heap, connect, header, download, parse, version, begin,
                              // verify, flash, end, set_boot, done
  const char* err = "OK";
  int http = 0;               // status line received, 0 when none arrived
  uint32_t bytes = 0;
  uint32_t total = 0;
  uint32_t ms = 0;            // the whole check or install
  uint32_t xferMs = 0;        // the HTTP transfer alone
  uint32_t idleMs = 0;        // since the last body byte (or the request) when the transfer ended
  uint32_t heap = 0;          // free heap, last sample while the connection was up
  uint32_t largest = 0;       // largest free block, same sample
  uint32_t largestMin = 0;    // smallest largest block seen during the transfer
  const char* wd = "none";    // idle: the read, handshake or header deadline; back: the user cancelled
  int rssi = 0;               // 0 when the station is no longer associated
};

// Where a transfer stopped, from the status line and whether the headers were read whole.
inline const char* transferStep(const int status, const bool headers) {
  if (!headers) return status == 0 ? "connect" : "header";
  return status == 200 ? "download" : "header";
}

// Which timeout ended a transfer. The TLS client gives up after timeoutMs without a byte.
inline const char* watchdog(const bool cancelled, const uint32_t idleMs, const uint32_t timeoutMs) {
  if (cancelled) return "back";
  return idleMs + 100 >= timeoutMs ? "idle" : "none";
}

inline size_t clampWritten(const int n, const size_t cap) {
  if (n < 0) return 0;
  return static_cast<size_t>(n) < cap ? static_cast<size_t>(n) : cap - 1;
}

// "ok=.. step=.. err=.. http=.. bytes=a/b ms=.. xms=.. kbs=.. heap=.. largest=.. lmin=.. idle=.. wd=.. rssi=.."
inline size_t formatFields(char* out, const size_t cap, const Attempt& a) {
  const uint32_t tenths =
      a.xferMs ? static_cast<uint32_t>(static_cast<uint64_t>(a.bytes) * 10000 / a.xferMs / 1024) : 0;
  return clampWritten(
      snprintf(out, cap,
               "ok=%d step=%s err=%s http=%d bytes=%u/%u ms=%u xms=%u kbs=%u.%u heap=%u largest=%u lmin=%u "
               "idle=%u wd=%s rssi=%d",
               a.ok ? 1 : 0, a.step, a.err, a.http, static_cast<unsigned>(a.bytes), static_cast<unsigned>(a.total),
               static_cast<unsigned>(a.ms), static_cast<unsigned>(a.xferMs), static_cast<unsigned>(tenths / 10),
               static_cast<unsigned>(tenths % 10), static_cast<unsigned>(a.heap), static_cast<unsigned>(a.largest),
               static_cast<unsigned>(a.largestMin), static_cast<unsigned>(a.idleMs), a.wd, a.rssi),
      cap);
}

// One card line: "<UTC or -> v=<version> op=<op> <fields>\n", always ending the line within cap.
inline size_t formatLine(char* out, const size_t cap, const long long utc, const char* version, const char* op,
                         const Attempt& a) {
  size_t n = 0;
  const time_t t = static_cast<time_t>(utc);
  struct tm parts {};
  if (utc >= 1735689600 && gmtime_r(&t, &parts)) {
    n = clampWritten(snprintf(out, cap, "%04d-%02d-%02dT%02d:%02d:%02dZ", parts.tm_year + 1900, parts.tm_mon + 1,
                              parts.tm_mday, parts.tm_hour, parts.tm_min, parts.tm_sec),
                     cap);
  } else {
    n = clampWritten(snprintf(out, cap, "-"), cap);
  }
  n += clampWritten(snprintf(out + n, cap - n, " v=%s op=%s ", version, op), cap - n);
  n += formatFields(out + n, cap - n, a);
  if (n + 1 >= cap) n = cap - 2;
  out[n++] = '\n';
  out[n] = '\0';
  return n;
}

// Which part of the old file stays when a line of newLen bytes is added: whole lines only,
// at most MAX_LINES - 1 of them, and MAX_BYTES in all. `cut` says old is the tail of a larger
// file, so its first line is partial. A last line without its newline was torn and goes.
struct Kept {
  size_t from = 0;
  size_t to = 0;
};
inline Kept keep(const char* old, const size_t len, const bool cut, const size_t newLen) {
  size_t end = len;
  while (end > 0 && old[end - 1] != '\n') --end;
  size_t begin = 0;
  if (cut) {
    const void* firstBreak = std::memchr(old, '\n', end);
    begin = firstBreak ? static_cast<size_t>(static_cast<const char*>(firstBreak) - old) + 1 : end;
  }
  size_t from = end;
  for (size_t count = 0; from > begin && count + 1 < MAX_LINES; ++count) {
    size_t start = from - 1;
    while (start > begin && old[start - 1] != '\n') --start;
    if (end - start + newLen > MAX_BYTES) break;
    from = start;
  }
  return {from, end};
}

// Device side (OtaLogFile.cpp). stage() formats the line now; writeStaged() puts it on the card
// and is handed to ActivityManager::deferWrite so it runs after the result screen is drawn.
void stage(const char* op, const Attempt& attempt);
void writeStaged();
}  // namespace ota_log
