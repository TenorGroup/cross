#include "ReadingStatsStore.h"

#include <HalClock.h>
#include <HalStorage.h>

#include <algorithm>
#include <cstdio>

#include "CrossPointSettings.h"
#include "util/NgayGio.h"

namespace {
constexpr const char* RESET_FILE = "/.crosspoint/reading-stats.reset";
bool finishReset() {
  if (!Storage.exists(RESET_FILE)) return true;
  const char* main = ReadingStatsStore::getFilePath();
  const std::string backup = std::string(main) + ".bak";
  if (Storage.exists(backup.c_str()) && !Storage.remove(backup.c_str())) return false;
  if (Storage.exists(main) && !Storage.remove(main)) return false;
  return Storage.rename(RESET_FILE, main);
}
void cleanupBookSnapshots() {
  auto dir = Storage.open("/.crosspoint/reading-stats");
  unsigned scanned = 0;
  for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    if (entry.isDirectory()) continue;
    char name[40];
    entry.getName(name, sizeof(name));
    const std::string filename = name;
    const bool suffix = filename.size() == 27 ||
                        (filename.size() == 31 && (filename.substr(27) == ".bak" || filename.substr(27) == ".tmp"));
    if (!suffix || filename.substr(0, 6) != "tenor_" || filename.substr(22, 5) != ".json") continue;
    bool hex = true;
    for (int i = 6; i < 22; ++i)
      hex &= (filename[i] >= '0' && filename[i] <= '9') || (filename[i] >= 'a' && filename[i] <= 'f');
    if (!hex) continue;
    entry.close();
    const auto path = std::string("/.crosspoint/reading-stats/") + filename;
    if (!Storage.remove(path.c_str())) LOG_ERR("STATS", "Old statistics cleanup deferred: %s", name);
    if (++scanned % 8 == 0) delay(1);
  }
}
enum class SnapshotReadResult { Ready, Invalid, Unavailable };

// Directory enumeration already opened this snapshot. Buffer its reads so JSON
// parsing avoids repeated full-path lookups and one SD call per character.
class SnapshotReader {
  HalFile& file;
  size_t remaining;
  uint8_t buffer[256];
  size_t used = 0, count = 0;
  bool failed = false;

 public:
  SnapshotReader(HalFile& file, size_t size) : file(file), remaining(size) {}
  int read() {
    if (used == count) {
      if (failed || remaining == 0) return -1;
      const size_t requested = std::min(remaining, sizeof(buffer));
      const int received = file.read(buffer, requested);
      if (received <= 0 || static_cast<size_t>(received) > requested) {
        failed = true;
        return -1;
      }
      remaining -= static_cast<size_t>(received);
      used = 0;
      count = static_cast<size_t>(received);
    }
    return buffer[used++];
  }
  size_t readBytes(char* target, size_t length) {
    size_t copied = 0;
    for (; copied < length; ++copied) {
      const int value = read();
      if (value < 0) break;
      target[copied] = static_cast<char>(value);
    }
    return copied;
  }
  bool finish() {
    // A JSON object can finish before the file does. Check the remaining bytes
    // too, matching the previous whole-file read's treatment of storage errors.
    while (read() >= 0) {}
    return !failed;
  }
};

SnapshotReadResult readOpenSnapshot(HalFile& file, size_t size, JsonDocument& doc, size_t maxBytes = 4096) {
  doc.clear();
  if (size > maxBytes) return file.close() ? SnapshotReadResult::Invalid : SnapshotReadResult::Unavailable;
  SnapshotReader input(file, size);
  const auto error = deserializeJson(doc, input);
  const bool complete = input.finish();
  const bool closed = file.close();
  if (!complete || !closed || error == DeserializationError::NoMemory || doc.overflowed()) {
    return SnapshotReadResult::Unavailable;
  }
  return !error && doc.is<JsonObject>() ? SnapshotReadResult::Ready : SnapshotReadResult::Invalid;
}

bool readSnapshot(const std::string& path, JsonDocument& doc) {
  using ReadResult = PersistableStoreBase::ReadResult;
  const auto result = PersistableStoreBase::readDocFromFileStatus(path.c_str(), doc, 32768);
  if (result == ReadResult::Unavailable) return false;
  if (result == ReadResult::Ready && doc.is<JsonObject>()) return true;
  doc.clear();
  const auto backup = PersistableStoreBase::readDocFromFileStatus((path + ".bak").c_str(), doc, 32768);
  if (backup == ReadResult::Ready && doc.is<JsonObject>()) return true;
  doc.clear();
  // writeSnapshot() commits .tmp whole before its renames, so a power cut between them
  // (an X3 on battery loses power in sleep) leaves the newest snapshot only there.
  return backup != ReadResult::Unavailable &&
         PersistableStoreBase::readDocFromFileStatus((path + ".tmp").c_str(), doc, 32768) == ReadResult::Ready &&
         doc.is<JsonObject>();
}
bool readResetSnapshot(JsonDocument& doc) {
  auto file = Storage.open(RESET_FILE);
  if (!file) return false;
  return readOpenSnapshot(file, file.size(), doc, 32768) == SnapshotReadResult::Ready;
}
bool writeSnapshot(const std::string& path, const JsonDocument& doc) {
  if (doc.overflowed()) return false;
  const std::string temporary = path + ".tmp", backup = path + ".bak";
  bool readable = false;
  if (Storage.exists(path.c_str())) {
    JsonDocument previous;
    const auto result = PersistableStoreBase::readDocFromFileStatus(path.c_str(), previous, 32768);
    if (result == PersistableStoreBase::ReadResult::Unavailable) return false;
    readable = result == PersistableStoreBase::ReadResult::Ready;
  }
  if (!PersistableStoreBase::writeDocToFile(temporary.c_str(), doc)) return false;
  if (Storage.exists(path.c_str())) {
    // Keep the known readable backup if the main file was interrupted.
    if (readable) {
      if (Storage.exists(backup.c_str()) && !Storage.remove(backup.c_str())) return false;
      if (!Storage.rename(path.c_str(), backup.c_str())) return false;
    } else if (!Storage.remove(path.c_str()))
      return false;
  }
  return Storage.rename(temporary.c_str(), path.c_str());
}
std::string bookFile(const std::string& path) {
  uint64_t hash = 14695981039346656037ULL;
  for (const unsigned char c : path) {
    hash ^= c;
    hash *= 1099511628211ULL;
  }
  char name[80];
  snprintf(name, sizeof(name), "/.crosspoint/reading-stats/tenor_%016llx.json", static_cast<unsigned long long>(hash));
  return name;
}
void writeBook(JsonVariant target, const std::string& path, const BookReadingRecord& b, const std::string& title,
               uint32_t epoch) {
  target["bookEpoch"] = epoch;
  target["path"] = path;
  target["title"] = title;
  target["minutes"] = b.minutes;
  target["ms"] = b.remainderMs;
  target["turns"] = b.turns;
  target["first"] = b.firstDay;
  target["last"] = b.lastDay;
  target["days"] = b.days;
  target["progress"] = b.progress;
  target["startProgress"] = b.startProgress;
}
BookReadingRecord decodeBook(JsonVariantConst doc) {
  BookReadingRecord b;
  b.minutes = doc["minutes"] | 0u;
  const uint32_t remainder = doc["ms"] | 0u;
  b.remainderMs = remainder < 60000 ? remainder : 0;
  b.turns = doc["turns"] | 0u;
  b.firstDay = doc["first"] | 0u;
  b.lastDay = doc["last"] | 0u;
  b.days = doc["days"] | 0u;
  b.progress = std::min<uint32_t>(doc["progress"] | 0u, 100);
  b.startProgress = std::min<uint32_t>(doc["startProgress"] | 0u, 100);
  return b;
}
}  // namespace

bool ReadingStatsStore::saveToFile() const {
  if (!writableSchema || !statisticsReadable) return false;
  std::lock_guard<std::mutex> lock(storeMutex);
  if (Storage.exists(RESET_FILE)) return false;
  JsonDocument doc;
  toJson(doc);
  return writeSnapshot(getFilePath(), doc);
}
bool ReadingStatsStore::loadFromFile() {
  std::lock_guard<std::mutex> lock(storeMutex);
  ++bookListGeneration;
  JsonDocument doc;
  const bool pending = Storage.exists(RESET_FILE);
  const bool exists =
      pending || Storage.exists(getFilePath()) || Storage.exists((std::string(getFilePath()) + ".bak").c_str());
  const bool loaded = (pending ? readResetSnapshot(doc) : readSnapshot(getFilePath(), doc)) &&
                      fromJson(doc.as<JsonVariantConst>());
  // With no global file there is nothing to protect: refusing here left the store
  // unreadable for good, and no reading was recorded again.
  statisticsReadable = loaded || !exists;
  if (pending && loaded) {
    const bool all = !doc["ngay"].is<JsonArrayConst>();
    if (finishReset() && all) cleanupBookSnapshots();
  }
  return loaded;
}

ReadingStatsStore::ResetResult ReadingStatsStore::resetStatistics(const bool all) {
  if (!writableSchema || !statisticsReadable || (all && bookEpoch == UINT32_MAX)) return ResetResult::Failed;
  std::lock_guard<std::mutex> lock(storeMutex);
  if (Storage.exists(RESET_FILE)) return ResetResult::Pending;
  JsonDocument doc;
  if (all) {
    doc["schema"] = 4;
    doc["bookEpoch"] = bookEpoch + 1;
  } else {
    toJson(doc);
    doc.remove("habits");
  }
  if (doc.overflowed()) return ResetResult::Failed;
  // Commit a preferred snapshot before touching any old main/backup file.
  const std::string temporary = std::string(RESET_FILE) + ".tmp";
  String encoded;
  serializeJson(doc, encoded);
  if (encoded.length() != measureJson(doc)) return ResetResult::Failed;
  auto file = Storage.open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
  if (!file || file.write(encoded.c_str(), encoded.length()) != encoded.length()) return ResetResult::Failed;
  if (!file.close() || !Storage.rename(temporary.c_str(), RESET_FILE)) return ResetResult::Failed;
  // The committed document was constructed here; parsing it cannot require more
  // title/path capacity than the current store in a habits-only reset.
  statisticsReadable = fromJson(doc.as<JsonVariantConst>());
  if (!statisticsReadable) return ResetResult::Pending;
  const bool finalized = finishReset();
  if (!finalized) {
    LOG_ERR("STATS", "Reset committed; journal finalization pending");
    return ResetResult::Pending;
  }
  if (all) cleanupBookSnapshots();
  return ResetResult::Complete;
}

// One reading of the clock, already moved onto the local offset. Both the day code and
// the minute of the day come from here, so the two can never name different moments.
static bool mocDiaPhuongBayGio(ngaygio::Moc& moc) {
  uint16_t year;
  uint8_t month, day, hour, minute;
  if (!halClock.getDateTime(year, month, day, hour, minute)) return false;
  if (year < 2000 || year > 2099 || month < 1 || month > 12 || day < 1 ||
      day > ngaygio::soNgayTrongThang(year, month) || hour > 23 || minute > 59)
    return false;
  const int offset = (std::min<int>(SETTINGS.clockUtcOffsetQ, 104) - 48) * 15;
  moc = ngaygio::doiSangDiaPhuong({year, month, day, hour, minute}, offset);
  return true;
}

uint32_t ReadingStatsStore::currentDay() {
  ngaygio::Moc moc;
  if (!mocDiaPhuongBayGio(moc)) return 0;
  return ngaygio::maNgay(moc);
}

uint16_t ReadingStatsStore::currentMinute() {
  ngaygio::Moc moc;
  if (!mocDiaPhuongBayGio(moc)) return NO_MINUTE;
  return static_cast<uint16_t>(moc.gio * 60 + moc.phut);
}

bool ReadingStatsStore::readBook(const std::string& path, BookReadingRecord& record) const {
  if (!statisticsReadable) {
    record = {};
    return false;
  }
  if (path == activeBookPath) {
    record = activeBook;
    return true;
  }
  JsonDocument doc;
  if (!readSnapshot(bookFile(path), doc) || (path != (doc["path"] | "")) || (doc["bookEpoch"] | 0u) != bookEpoch) {
    record = {};
    return false;
  }
  record = decodeBook(doc.as<JsonVariantConst>());
  return true;
}

bool ReadingStatsStore::activateBook(const std::string& path, const uint8_t progress, const std::string& title) {
  // Each X3 wake is a boot. A read the boot could not finish is retried here, or the
  // whole session up to the next sleep would go unrecorded.
  if (!statisticsReadable) loadFromFile();
  if (!writableSchema || !statisticsReadable) return false;
  if (path == activeBookPath) {
    if (!title.empty() && title != activeBookTitle) {
      activeBookTitle = title;
      ++bookListGeneration;
    }
    return true;
  }
  // The active book and global days share one checkpoint. Archive the last
  // committed book before replacing it; a crash can only replay an identical snapshot.
  if (!activeBookPath.empty()) {
    JsonDocument doc;
    writeBook(doc.to<JsonObject>(), activeBookPath, activeBook, activeBookTitle, bookEpoch);
    if (!Storage.ensureDirectoryExists("/.crosspoint/reading-stats") || !writeSnapshot(bookFile(activeBookPath), doc))
      return false;
  }
  BookReadingRecord next;
  const bool exists = readBook(path, next);
  if (!exists) next.startProgress = progress;
  activeBookPath = path;
  activeBookTitle = title;
  activeBook = next;
  ++bookListGeneration;
  return true;
}

void ReadingStatsStore::record(const uint32_t day, const uint32_t ms, const uint16_t turns, const uint8_t progress) {
  kho.gopMilliseconds(day, ms, turns);
  const uint64_t total =
      std::min<uint64_t>(static_cast<uint64_t>(activeBook.minutes) * 60000 + activeBook.remainderMs + ms,
                         static_cast<uint64_t>(UINT32_MAX) * 60000 + 59999);
  activeBook.minutes = total / 60000;
  activeBook.remainderMs = total % 60000;
  activeBook.turns += std::min<uint32_t>(turns, UINT32_MAX - activeBook.turns);
  activeBook.progress = std::min<uint8_t>(progress, 100);
  if ((ms || turns) && day && day > activeBook.lastDay) {
    if (!activeBook.firstDay) activeBook.firstDay = day;
    activeBook.lastDay = day;
    ++bookListGeneration;
    if (activeBook.days < UINT32_MAX) ++activeBook.days;
  }
}

void ReadingStatsStore::toJson(JsonDocument& doc) const {
  doc["schema"] = bookEpoch ? 4 : 3;
  doc["bookEpoch"] = bookEpoch;
  auto h = doc["habits"].to<JsonObject>();
  h["clockLost"] = habitLedger.clockLost;
  h["qualityGap"] = habitLedger.qualityGapDay;
  h["first"] = habitLedger.firstDay;
  h["evaluated"] = habitLedger.lastEvaluated;
  h["awarded"] = habitLedger.awarded;
  h["hidden"] = habitLedger.hidden;
  auto counters = h["gates"].to<JsonArray>();
  for (size_t i = 0; i < habits::NAMES; ++i) {
    counters.add(habitLedger.enter[i]);
    counters.add(habitLedger.leave[i]);
  }
  auto days = h["days"].to<JsonArray>();
  for (const auto& d : habitLedger.days)
    if (d.day) {
      auto row = days.add<JsonArray>();
      row.add(d.day);
      row.add(d.activeMs);
      row.add(d.nightMs);
      row.add(d.earlyMs);
      row.add(d.sessions);
      row.add(d.shortSessions);
      row.add(d.longSessions);
      row.add(d.uncertain);
      row.add(d.night);
    }
  const auto& s = habitLedger.session;
  if (s.visibleMs) {
    auto a = h["session"].to<JsonArray>();
    a.add(s.visibleMs);
    a.add(s.activeMs);
    a.add(s.idleMs);
    a.add(s.turns);
    a.add(s.last.day);
    a.add(s.last.utcMinute);
    a.add(s.last.minute);
    a.add(s.last.offset);
    a.add(s.uncertain);
    a.add(s.clean);
  }
  if (!activeBookPath.empty())
    writeBook(doc["activeBook"].to<JsonObject>(), activeBookPath, activeBook, activeBookTitle, bookEpoch);
  JsonArray ds = doc["ngay"].to<JsonArray>();
  for (const auto& n : kho.cacNgay()) {
    JsonArray dong = ds.add<JsonArray>();
    dong.add(n.ma);
    dong.add(n.phut);
    dong.add(n.trang);
    if (n.leMs) dong.add(n.leMs);
  }
  // Phan doc duoc luc dong ho chua tung dong bo. Giu rieng de tong van dung ma khong ai
  // phai gan bua no vao mot ngay.
  if (kho.msChuaBietNgay()) doc["lacMs"] = kho.msChuaBietNgay();
  if (kho.phutChuaBietNgay() > 0 || kho.trangChuaBietNgay() > 0) {
    doc["lacPhut"] = kho.phutChuaBietNgay();
    doc["lacTrang"] = kho.trangChuaBietNgay();
  }
}

bool ReadingStatsStore::fromJson(const JsonVariantConst doc) {
  ++bookListGeneration;
  const auto schema = doc["schema"];
  writableSchema = schema.isUnbound() || (schema.is<uint32_t>() && schema.as<uint32_t>() <= 4);
  if (!writableSchema) {
    LOG_ERR("STATS", "Unsupported statistics schema retained without changes");
    return false;
  }
  bookEpoch = doc["bookEpoch"] | 0u;
  habitLedger.clear();
  const auto h = doc["habits"];
  habitLedger.clockLost = h["clockLost"] | false;
  habitLedger.qualityGapDay = h["qualityGap"] | 0u;
  habitLedger.firstDay = h["first"] | 0u;
  habitLedger.lastEvaluated = h["evaluated"] | 0u;
  habitLedger.awarded = (h["awarded"] | 0u) & 63;
  habitLedger.hidden = (h["hidden"] | 0u) & 63;
  for (size_t i = 0; i < habits::NAMES; ++i) {
    habitLedger.enter[i] = std::min<unsigned>(3, h["gates"][2 * i] | 0u);
    habitLedger.leave[i] = std::min<unsigned>(3, h["gates"][2 * i + 1] | 0u);
  }
  size_t count = 0;
  for (const auto row : h["days"].as<JsonArrayConst>()) {
    if (count == habits::DAYS) break;
    if (!row.is<JsonArrayConst>() || row.size() != 9 || !row[0].is<uint32_t>() || !row[0].as<uint32_t>() ||
        row[0].as<uint32_t>() > 36525)
      continue;
    bool valid = true;
    for (int i = 1; i < 8; ++i)
      if (!row[i].is<uint32_t>()) valid = false;
    if (!valid || row[1].as<uint32_t>() > 86400000 || row[2].as<uint32_t>() > row[1].as<uint32_t>() ||
        row[3].as<uint32_t>() > row[1].as<uint32_t>())
      continue;
    for (size_t i = 0; i < count; ++i)
      if (habitLedger.days[i].day == row[0].as<uint32_t>()) valid = false;
    if (!valid) continue;
    auto& d = habitLedger.days[count++];
    d.day = row[0];
    d.activeMs = row[1];
    d.nightMs = row[2];
    d.earlyMs = row[3];
    d.sessions = std::min<unsigned>(UINT16_MAX, row[4].as<uint32_t>());
    d.shortSessions = std::min<unsigned>(d.sessions, row[5].as<uint32_t>());
    d.longSessions = std::min<unsigned>(d.sessions, row[6].as<uint32_t>());
    d.uncertain = std::min<unsigned>(d.sessions, row[7].as<uint32_t>());
    d.night = row[8] | false;
  }
  const auto a = h["session"].as<JsonArrayConst>();
  if (a.size() == 10 && a[0].is<uint32_t>() && a[1].is<uint32_t>() && a[0].as<uint32_t>() <= 86400000 &&
      a[1].as<uint32_t>() <= a[0].as<uint32_t>()) {
    auto& s = habitLedger.session;
    s.visibleMs = a[0];
    s.activeMs = a[1];
    s.idleMs = a[2] | 0u;
    s.turns = a[3] | 0u;
    s.last.day = a[4] | 0u;
    s.last.utcMinute = a[5] | 0u;
    s.last.minute = a[6] | 0u;
    s.last.offset = a[7] | 0;
    s.uncertain = a[8] | true;
    s.clean = a[9] | false;
    if (s.last.day > 36525 || s.last.minute >= 1440 || s.last.offset < -720 || s.last.offset > 840) s = {};
    habitLedger.restored = true;
  }
  kho.xoaHet();
  activeBookPath = doc["activeBook"]["path"] | "";
  activeBookTitle = doc["activeBook"]["title"] | "";
  activeBook = decodeBook(doc["activeBook"]);
  if (doc["ngay"].is<JsonArrayConst>()) {
    for (const JsonVariantConst dong : doc["ngay"].as<JsonArrayConst>()) {
      if (!dong.is<JsonArrayConst>()) continue;
      const JsonArrayConst o = dong.as<JsonArrayConst>();
      if (o.size() < 3) continue;
      if (!o[0].is<uint32_t>() || !o[1].is<uint16_t>() || !o[2].is<uint16_t>()) continue;
      const auto ma = o[0].as<uint32_t>();
      // Ma ngay bang 0 la rac: phan chua biet ngay co thung rieng, khong nam trong day.
      if (ma == 0) continue;
      // Use the same merge and retention rule as live updates, in any input order.
      kho.gopThem(ma, o[1].as<uint16_t>(), o[2].as<uint16_t>());
      if (o.size() >= 4 && o[3].is<uint16_t>() && o[3].as<uint16_t>() < 60000)
        kho.gopMilliseconds(ma, o[3].as<uint16_t>());
    }
  }
  kho.datPhanLac(doc["lacPhut"] | 0u, doc["lacTrang"] | 0u);
  const uint32_t remainder = doc["lacMs"] | 0u;
  kho.datMsChuaBietNgay(remainder < 60000 ? remainder : 0);
  return true;
}

habits::Stamp ReadingStatsStore::habitStamp() {
  uint16_t year;
  uint8_t month, day, hour, minute;
  if (!halClock.getDateTime(year, month, day, hour, minute) || hour > 23 || minute > 59)
    return {};
  const auto utcDay = habits::ordinal(year, month, day);
  if (!utcDay) return {};
  const int offset = (std::min<int>(SETTINGS.clockUtcOffsetQ, 104) - 48) * 15;
  auto local = ngaygio::doiSangDiaPhuong({year, month, day, hour, minute}, offset);
  return {habits::ordinal(local.nam, local.thang, local.ngay), (utcDay - 1) * 1440 + hour * 60u + minute,
          static_cast<uint16_t>(local.gio * 60 + local.phut), static_cast<int16_t>(offset)};
}
void ReadingStatsStore::observeHabits(uint32_t elapsed, uint16_t turns, uint32_t now) {
  if (!writableSchema || !statisticsReadable) return;
  const auto stamp = habitStamp();
  habitLedger.observe(elapsed, turns, stamp, now);
  habitLedger.evaluate(stamp.day);
}
void ReadingStatsStore::prepareHabits() {
  if (!writableSchema || !statisticsReadable) return;
  const auto stamp = habitStamp();
  habitLedger.settle(stamp);
  habitLedger.evaluate(stamp.day, true);
}
void ReadingStatsStore::markHabitsSleep() {
  if (!writableSchema || !statisticsReadable || !habitLedger.session.visibleMs) return;
  habitLedger.session.clean = true;
  saveToFile();
}

void ReadingStatsStore::listBooks(const BookEntry& boundary, bool previous, std::vector<BookEntry>& result) const {
  result.clear();
  if (!statisticsReadable) return;
  result.reserve(22);
  const auto less = [](const BookEntry& a, const BookEntry& b) {
    return a.day != b.day ? a.day > b.day : a.path < b.path;
  };
  const auto add = [&](BookEntry entry) {
    if (entry.path.empty() || entry.path.size() > 1024) return;
    if (!boundary.path.empty() && !(previous ? less(entry, boundary) : less(boundary, entry))) return;
    if (entry.title.empty()) entry.title = entry.path.substr(entry.path.find_last_of('/') + 1);
    if (entry.title.size() > 256) {
      entry.title.resize(256);
      while (!entry.title.empty() && (static_cast<unsigned char>(entry.title.back()) & 0xc0) == 0x80)
        entry.title.pop_back();
      if (!entry.title.empty() && static_cast<unsigned char>(entry.title.back()) >= 0xc0) entry.title.pop_back();
    }
    result.insert(std::lower_bound(result.begin(), result.end(), entry, less), std::move(entry));
    if (result.size() > 21) {
      if (previous)
        result.erase(result.begin());
      else
        result.pop_back();
    }
  };
  if (!activeBookPath.empty()) add({activeBookPath, activeBookTitle, activeBook.lastDay});
  auto dir = Storage.open("/.crosspoint/reading-stats");
  if (!dir || !dir.isDirectory()) return;
  char name[40];
  unsigned scanned = 0;
  for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
    if (file.isDirectory()) continue;
    file.getName(name, sizeof(name));
    const std::string filename = name;
    if (++scanned % 16 == 0) delay(1);
    const bool backup = filename.size() == 31 && filename.substr(27) == ".bak";
    const auto base = backup ? filename.substr(0, 27) : filename;
    if (base.size() != 27 || base.substr(0, 6) != "tenor_" || base.substr(22) != ".json") continue;
    const auto snapshot = std::string("/.crosspoint/reading-stats/") + base;
    const size_t size = file.size();
    if (size > 4096 || (backup && Storage.exists(snapshot.c_str()))) continue;
    JsonDocument doc;
    const auto read = readOpenSnapshot(file, size, doc);
    if (read == SnapshotReadResult::Unavailable) continue;
    // Corrupt snapshots retain the existing .davbak/.bak recovery policy.
    // Read errors and OOM say nothing about corruption, so keep both copies.
    if ((read == SnapshotReadResult::Ready || readSnapshot(snapshot, doc)) &&
        (doc["bookEpoch"] | 0u) == bookEpoch) {
      const std::string path = doc["path"] | "";
      if (path != activeBookPath && path.size() <= 1024 && bookFile(path) == snapshot)
        add({path, doc["title"] | "", doc["last"] | 0u});
    }
  }
}
