#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <cstring>
#include <ctime>
#include <new>

#include "OtaLog.h"

namespace ota_log {
namespace {
// Heap, not a static buffer: the line lives only from the result to the card write.
char* staged = nullptr;
}  // namespace

void stage(const char* op, const Attempt& attempt) {
  delete[] staged;
  staged = new (std::nothrow) char[LINE_BYTES];
  if (staged) formatLine(staged, LINE_BYTES, static_cast<long long>(time(nullptr)), CROSSPOINT_VERSION, op, attempt);
}

// Rewrites the small file with its newest lines and the staged one. No temporary file: a
// power cut mid-write leaves a torn last line, which keep() drops on the next write.
void writeStaged() {
  if (!staged) return;
  const size_t added = strlen(staged);
  auto old = makeUniqueNoThrow<char[]>(MAX_BYTES);
  if (!old) {
    LOG_ERR("OTA", "No heap for the OTA log");
  } else {
    size_t len = 0;
    bool cut = false;
    HalFile in;
    if (Storage.openFileForRead("OTA", PATH, in)) {
      const size_t size = in.fileSize();
      cut = size > MAX_BYTES;
      if (!cut || in.seekSet(size - MAX_BYTES)) {
        const int n = in.read(old.get(), MAX_BYTES);
        len = n > 0 ? static_cast<size_t>(n) : 0;
      }
      in.close();
    }
    const Kept kept = keep(old.get(), len, cut, added);
    HalFile out;
    bool ok = Storage.openFileForWrite("OTA", PATH, out);
    if (ok) {
      ok = out.write(old.get() + kept.from, kept.to - kept.from) == kept.to - kept.from &&
           out.write(staged, added) == added;
      ok = out.close() && ok;
    }
    if (!ok) LOG_ERR("OTA", "OTA log write failed");
  }
  delete[] staged;
  staged = nullptr;
}
}  // namespace ota_log
