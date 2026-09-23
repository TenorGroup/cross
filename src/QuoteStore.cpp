#include "QuoteStore.h"

#include <HalStorage.h>
#include <Logging.h>
#include <PersistableStore.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
namespace quotes {
namespace {
constexpr char DIRECTORY[] = "/.crosspoint/quotes";
constexpr char MARKER[] = "/.crosspoint/quotes/.ten-v2";
constexpr unsigned SLOTS = 16;

std::string filePath(const std::string& name) { return std::string(DIRECTORY) + "/" + name; }

// Days since 2020-01-01 on a calendar of 31-day months, so the code grows with the date
// and fits four hex digits until 2196. Unknown and pre-2020 days are 0 and sort oldest.
uint32_t dayCode(const uint32_t day) {
  const uint32_t year = day / 10000, month = day / 100 % 100, date = day % 100;
  if (year < 2020 || month < 1 || month > 12 || date < 1 || date > 31) return 0;
  return std::min<uint32_t>((year - 2020) * 372 + (month - 1) * 31 + (date - 1), 0xFFFF);
}

// Id of `q` in slot 0 of its own book, day and minute.
QuoteId baseId(const QuoteRecord& q) {
  const uint64_t minute = q.minute < 1440 ? q.minute : 0;
  return static_cast<uint64_t>(bookKey(q.path)) << 32 | static_cast<uint64_t>(dayCode(q.day)) << 16 | minute << 4;
}

// Day, minute and slot fill the low 32 bits, so comparing them orders moments. Quotes of
// different books kept in the same slot of the same minute fall back to id order, which
// keeps the order total and the listing stable.
bool newerFirst(const QuoteId a, const QuoteId b) {
  const auto momentA = static_cast<uint32_t>(a), momentB = static_cast<uint32_t>(b);
  return momentA != momentB ? momentA > momentB : a < b;
}

uint32_t minuteOf(const QuoteId id) { return static_cast<uint32_t>(id >> 4) & 0xFFF; }

// First id of `base`'s book and day that `taken` does not hold, in `base`'s own minute or,
// when its 16 slots are full, the next minute with a free slot up to the last of the day.
// Only the name moves: the record keeps its own minute, so the time shown stays exact and
// the order is off by minutes only among quotes kept in such a burst.
bool freeId(const QuoteId base, const std::vector<QuoteId>& taken, QuoteId& id) {
  for (QuoteId minute = minuteOf(base); minute < 1440; minute++) {
    for (unsigned slot = 0; slot < SLOTS; slot++) {
      id = (base & ~QuoteId{0xFFFF}) | minute << 4 | slot;
      if (std::find(taken.begin(), taken.end(), id) == taken.end()) return true;
    }
  }
  return false;
}

// A name fits its record when book and day match and its minute is the record's own or a
// later one freeId() moved it to.
bool fits(const QuoteId id, const QuoteId base) { return id >> 16 == base >> 16 && minuteOf(id) >= minuteOf(base); }

// Every file name in the directory, in directory order. The entry is released before
// `visit` runs, because the directory walk already holds one handle on the file and
// `visit` may open it again by path.
template <typename Visit>
void forEachEntry(Visit&& visit) {
  auto directory = Storage.open(DIRECTORY);
  if (!directory || !directory.isDirectory()) return;
  char name[32];
  for (auto entry = directory.openNextFile(); entry; entry = directory.openNextFile()) {
    if (entry.isDirectory()) continue;
    entry.getName(name, sizeof(name));
    entry.close();
    if (!visit(static_cast<const char*>(name))) return;
  }
}

// Every quote in the directory, until `visit` returns false.
template <typename Visit>
void forEachId(Visit&& visit) {
  forEachEntry([&](const char* name) {
    QuoteId id;
    return !idOf(name, id) || visit(id);
  });
}

bool acceptable(const QuoteRecord& q) {
  return !q.text.empty() && q.text.size() <= MAX_BYTES && !q.path.empty() && q.path.size() <= 1024 &&
         q.title.size() <= 512;
}

bool writeRecord(const std::string& path, const QuoteRecord& q) {
  JsonDocument doc;
  doc["schema"] = 1;
  doc["path"] = q.path;
  doc["title"] = q.title;
  doc["text"] = q.text;
  doc["spine"] = q.spine;
  doc["page"] = q.page;
  doc["day"] = q.day;
  if (q.minute < 1440) doc["gio"] = q.minute;
  if (q.hasAnchor) {
    doc["vo"] = q.anchorStart;
    doc["ve"] = q.anchorEnd;
  }
  return !doc.overflowed() && PersistableStoreBase::writeDocToFile(path.c_str(), doc);
}
bool readRecord(const std::string& filePath, QuoteRecord& q) {
  JsonDocument doc;
  {
    auto file = Storage.open(filePath.c_str());
    if (!file || file.size() > 16384) return false;
  }
  if (!PersistableStoreBase::readDocFromFile(filePath.c_str(), doc)) return false;
  if ((doc["schema"] | 0) != 1) return false;
  const char* text = doc["text"] | "";
  const char* path = doc["path"] | "";
  const char* title = doc["title"] | "";
  if (!*text || strlen(text) > MAX_BYTES || strlen(path) > 1024 || strlen(title) > 512) return false;
  q.text = text;
  q.path = path;
  q.title = title;
  q.spine = doc["spine"] | 0;
  q.page = doc["page"] | 0;
  q.day = doc["day"] | 0u;
  // Optional inside the same schema, like the anchor below: a record kept before the time
  // was stamped has no "gio" key and is shown with its date alone.
  q.minute = doc["gio"].isNull() ? NO_MINUTE : static_cast<uint16_t>(doc["gio"] | 0u);
  if (q.minute >= 1440) q.minute = NO_MINUTE;
  // Optional inside the same schema: records written before highlighting existed have
  // no anchor, load normally, and are simply never drawn on a page.
  q.hasAnchor = !doc["vo"].isNull() && !doc["ve"].isNull();
  q.anchorStart = doc["vo"] | 0u;
  q.anchorEnd = doc["ve"] | 0u;
  return true;
}

// Finishes or undoes a write that save() or replace() staged as "<name>.tmp" when the power
// went. replace() removes the old record only after the staged file is written and closed,
// so a staged file with no record beside it is complete and is promoted. A staged file
// next to its record was cut while being written and is dropped. save() stages a new quote
// the same way; a cut in the middle of that write leaves a partial file with no record,
// which is dropped too, because promoting it would list a quote that cannot be opened.
void repairStaged() {
  std::vector<QuoteId> staged;
  forEachEntry([&](const char* entry) {
    QuoteId id;
    if (strlen(entry) == 25 && strcmp(entry + 21, ".tmp") == 0 && idOf(std::string(entry, 21), id))
      staged.push_back(id);
    return true;
  });
  for (const QuoteId id : staged) {
    const std::string path = filePath(nameOf(id));
    const std::string temporary = path + ".tmp";
    QuoteRecord record;
    const bool promote = !Storage.exists(path.c_str()) && readRecord(temporary, record);
    const bool done = promote ? Storage.rename(temporary.c_str(), path.c_str()) : Storage.remove(temporary.c_str());
    LOG_INF("QTS", "Staged quote %s %s%s", nameOf(id).c_str(), promote ? "promoted" : "dropped",
            done ? "" : ", failed");
  }
}
}  // namespace

std::string nameOf(const QuoteId id) {
  char name[24];
  snprintf(name, sizeof(name), "%016llx.json", static_cast<unsigned long long>(id));
  return name;
}
bool idOf(const std::string& name, QuoteId& id) {
  if (!validName(name)) return false;
  QuoteId value = 0;
  for (size_t i = 0; i < 16; i++) {
    const char c = name[i];
    value = value << 4 | static_cast<QuoteId>(c <= '9' ? c - '0' : c - 'a' + 10);
  }
  id = value;
  return true;
}
uint32_t bookKey(const std::string& path) {
  uint32_t hash = 2166136261u;
  for (const unsigned char c : path) {
    hash ^= c;
    hash *= 16777619u;
  }
  return hash;
}
uint32_t bookKeyOfName(const QuoteId id) { return static_cast<uint32_t>(id >> 32); }
uint32_t momentOfName(const QuoteId id) { return static_cast<uint32_t>(id >> 4) & 0x0FFFFFFF; }
uint32_t bookKeyOfName(const std::string& name) {
  QuoteId id;
  return idOf(name, id) ? bookKeyOfName(id) : 0;
}
uint32_t momentOfName(const std::string& name) {
  QuoteId id;
  return idOf(name, id) ? momentOfName(id) : 0;
}
bool validName(const std::string& name) {
  return name.size() == 21 && name.compare(16, 5, ".json") == 0 &&
         name.find_first_not_of("0123456789abcdef") == 16;
}
bool load(const QuoteId id, QuoteRecord& q) { return readRecord(filePath(nameOf(id)), q); }
bool save(const QuoteRecord& q) {
  if (!acceptable(q)) return false;
  const QuoteId base = baseId(q);
  // The same words kept again, even on another day, belong to this book's files, so only
  // those are opened; the directory walk finishes before any of them is.
  std::vector<QuoteId> sameBook;
  forEachId([&](const QuoteId id) {
    if (bookKeyOfName(id) == bookKeyOfName(base)) sameBook.push_back(id);
    return true;
  });
  for (const QuoteId id : sameBook) {
    QuoteRecord existing;
    if (!load(id, existing) || existing.path != q.path || existing.text != q.text || existing.spine != q.spine ||
        existing.page != q.page)
      continue;
    if (existing.hasAnchor || !q.hasAnchor) return true;
    // A quote saved before anchors existed is rewritten once under its own name, so
    // highlighting the same words again starts drawing them and the quote keeps the
    // moment it was first kept.
    existing.hasAnchor = true;
    existing.anchorStart = q.anchorStart;
    existing.anchorEnd = q.anchorEnd;
    return replace(id, existing);
  }
  QuoteId id;
  if (!freeId(base, sameBook, id)) return false;
  if (!Storage.ensureDirectoryExists("/.crosspoint") || !Storage.ensureDirectoryExists(DIRECTORY)) return false;
  const std::string path = filePath(nameOf(id));
  const std::string temporary = path + ".tmp";
  return writeRecord(temporary, q) && Storage.rename(temporary.c_str(), path.c_str());
}
bool remove(const QuoteId id) { return Storage.remove(filePath(nameOf(id)).c_str()); }
bool replace(const QuoteId id, const QuoteRecord& updated) {
  if (!acceptable(updated)) return false;
  const std::string path = filePath(nameOf(id));
  if (!Storage.exists(path.c_str())) return false;
  const std::string temporary = path + ".tmp";
  if (!writeRecord(temporary, updated)) return false;
  // rename does not replace, so the old record goes first; the staged file already holds
  // every field of the new one.
  return Storage.remove(path.c_str()) && Storage.rename(temporary.c_str(), path.c_str());
}
bool load(const std::string& name, QuoteRecord& quote) {
  QuoteId id;
  return idOf(name, id) && load(id, quote);
}
bool remove(const std::string& name) {
  QuoteId id;
  return idOf(name, id) && remove(id);
}
bool replace(const std::string& name, const QuoteRecord& updated) {
  QuoteId id;
  return idOf(name, id) && replace(id, updated);
}
bool migrateNames() {
  repairStaged();
  if (Storage.exists(MARKER)) return true;
  // Ids first, renames after: moving files while the directory is being walked could
  // show a moved file a second time or skip the one after it.
  std::vector<QuoteId> ids;
  forEachId([&](const QuoteId id) {
    ids.push_back(id);
    return true;
  });
  std::vector<QuoteId> taken = ids;
  size_t renamed = 0, unreadable = 0;
  bool failed = false;
  for (const QuoteId id : ids) {
    QuoteRecord quote;
    // A record that cannot be read has no fields to name it by. It stays where it is and
    // does not hold the marker back, or every visit would retry it forever.
    if (!load(id, quote)) {
      unreadable++;
      continue;
    }
    const QuoteId base = baseId(quote);
    if (fits(id, base)) continue;
    QuoteId target;
    if (!freeId(base, taken, target) ||
        !Storage.rename(filePath(nameOf(id)).c_str(), filePath(nameOf(target)).c_str())) {
      failed = true;
      continue;
    }
    *std::find(taken.begin(), taken.end(), id) = target;
    renamed++;
  }
  LOG_INF("QTS", "Quote names: %u renamed, %u unreadable left as they were", static_cast<unsigned>(renamed),
          static_cast<unsigned>(unreadable));
  if (failed) {
    LOG_ERR("QTS", "Quote names: a rename failed, will retry");
    return false;
  }
  return Storage.ensureDirectoryExists("/.crosspoint") && Storage.ensureDirectoryExists(DIRECTORY) &&
         Storage.writeFile(MARKER, "2");
}
void listNames(const uint32_t book, std::vector<QuoteId>& ids) {
  ids.clear();
  forEachId([&](const QuoteId id) {
    if (book != 0 && bookKeyOfName(id) != book) return true;
    if (ids.size() >= MAX_QUOTES && !newerFirst(id, ids.back())) return true;
    ids.insert(std::upper_bound(ids.begin(), ids.end(), id, newerFirst), id);
    if (ids.size() > MAX_QUOTES) ids.pop_back();
    return true;
  });
}
void listBooks(std::vector<BookSummary>& books) {
  books.clear();
  forEachId([&](const QuoteId id) {
    const uint32_t book = bookKeyOfName(id);
    const auto found =
        std::find_if(books.begin(), books.end(), [book](const BookSummary& summary) { return summary.book == book; });
    if (found == books.end()) {
      BookSummary summary;
      summary.book = book;
      summary.count = 1;
      summary.newest = id;
      books.push_back(summary);
      return true;
    }
    if (found->count < UINT16_MAX) found->count++;
    if (newerFirst(id, found->newest)) found->newest = id;
    return true;
  });
  std::sort(books.begin(), books.end(),
            [](const BookSummary& a, const BookSummary& b) { return newerFirst(a.newest, b.newest); });
}
void loadAnchors(const std::string& bookPath, std::vector<QuoteAnchor>& anchors) {
  anchors.clear();
  if (bookPath.empty()) return;
  const uint32_t book = bookKey(bookPath);
  forEachId([&](const QuoteId id) {
    if (bookKeyOfName(id) != book) return true;
    QuoteRecord quote;
    // Two paths can share a key, so the path inside the record still decides.
    if (load(id, quote) && quote.hasAnchor && quote.path == bookPath)
      anchors.push_back(QuoteAnchor{static_cast<int32_t>(quote.spine), quote.anchorStart, quote.anchorEnd});
    return anchors.size() < MAX_BOOK_ANCHORS;
  });
}
}  // namespace quotes
