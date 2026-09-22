#include <cassert>
#include <string>

#include <Logging.h>

#include "../../lib/hal/HalStorage.h"

#ifdef TENOR_UI_ACCEPTANCE
static int matching(const std::string& text) {
  int count = 0;
  for (const auto& line : traceLines) if (line.find(text) != std::string::npos) ++count;
  return count;
}
#endif

int main() {
  auto& storage = HalStorage::getInstance();
  char buffer[8] = {};
  {
    HalFile file;
    assert(storage.openFileForRead("Reader", "/book.epub", file));
    assert(file.read(buffer, 3) == 3);
    assert(file.read() == 'd');
    assert(file.read(buffer, 1) == 0);
    assert(file.seek(0));
    assert(file.seek64(1));
    assert(file.seekCur(1));
    assert(!file.seekSet(99));
    assert(file.sync());
    file.flush();
    assert(file.close());
    assert(file.close());
  }
  {
    auto file = storage.open("/shortwrite", O_RDONLY);
    const uint8_t bytes[] = {1, 2, 3, 4};
    assert(file.write(bytes, 4) == 2);
    assert(file.write(static_cast<const void*>(bytes), 2) == 2);
    assert(file.write(static_cast<uint8_t>(5)) == 1);
  }
  {
    HalFile file;
    assert(storage.openFileForWrite("Cache", "/cache.bin", file));
    assert(file.write(static_cast<uint8_t>(7)) == 1);
  }
  {
    HalFile file;
    assert(!storage.openFileForRead("Reader", "/missing", file));
  }
  {
    auto dir = storage.open("/dir");
    auto child = dir.openNextFile();
    assert(child.isOpen());
  }

#ifdef TENOR_UI_ACCEPTANCE
  assert(matching("SD_TRACE] OPEN") == 6);
  assert(matching("SD_TRACE] CLOSE ") == 6);
  assert(matching("SD_TRACE] CLOSE_DETAIL") == 6);
  assert(matching("SD_TRACE] CLOSE_TIME") == 6);
  assert(matching("mode=read flags=0x0 ok=1 module=Reader path=/book.epub") == 1);
  assert(matching("mode=write flags=0x0 ok=1 module=Cache path=/cache.bin") == 1);
  assert(matching("mode=read flags=0x0 ok=0 module=Reader path=/missing") == 1);
  assert(matching("parent=5 mode=next flags=0x0 ok=1 module=HalFile path=child.epub") == 1);
  assert(matching("via=explicit ok=1") == 1);
  assert(matching("r_calls=3 r_bytes=4") == 1);
  assert(matching("r_us=30") == 1);
  assert(matching("r_short=1 r_err=0") == 1);
  assert(matching("w_calls=3 w_bytes=5") == 1);
  assert(matching("w_us=30") == 1);
  assert(matching("w_short=1 w_err=1") == 1);
  assert(matching("seek_calls=4") == 1);
  assert(matching("seek_us=40") == 1);
  assert(matching("seek_err=1 sync_calls=1") == 1);
  assert(matching("sync_err=0 flush_calls=1") == 1);
  assert(matching("sync_us=10") == 1);
  for (const auto& line : traceLines) assert(line.size() < 255);
#else
  assert(traceLines.empty());
#endif
}
