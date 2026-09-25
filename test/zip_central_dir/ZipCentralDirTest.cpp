// A book's zip central directory, walked by every item lookup. A book of 5.000 chapters has about
// 5.000 entries there, and its OPF and cover sit at the end: each lookup read every entry header
// in about thirteen small reads, each taking the card's lock (2,3 s on the X3 to find a cover), and
// the reader looks each item up twice (its size, then its bytes), each time from a new ZipFile.
// These cases hold the walk to block reads, a repeated lookup to no walk at all, and the answers
// to what the zip says, damaged zips included.
#include <Arduino.h>
#include <HalStorage.h>
#include <ZipFile.h>
#include <gtest/gtest.h>

#include <cstdio>
#include <string>
#include <vector>

EspClass ESP;

namespace {
uint32_t crc32(const std::string& s) {
  uint32_t c = 0xFFFFFFFFu;
  for (const unsigned char b : s) {
    c ^= b;
    for (int i = 0; i < 8; i++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
  }
  return ~c;
}

void put16(std::vector<uint8_t>& out, uint32_t v) {
  out.push_back(v & 0xFF);
  out.push_back((v >> 8) & 0xFF);
}
void put32(std::vector<uint8_t>& out, uint32_t v) {
  put16(out, v & 0xFFFF);
  put16(out, v >> 16);
}

struct Entry {
  std::string name, data, extra, comment;
};

// A stored zip of these entries, as a book on the card holds it.
std::vector<uint8_t> makeZip(const std::vector<Entry>& entries) {
  std::vector<uint8_t> out, dir;
  for (const auto& e : entries) {
    const uint32_t local = static_cast<uint32_t>(out.size());
    put32(out, 0x04034b50);
    put16(out, 20);
    put16(out, 0);
    put16(out, 0);  // stored
    put32(out, 0);
    put32(out, crc32(e.data));
    put32(out, e.data.size());
    put32(out, e.data.size());
    put16(out, e.name.size());
    put16(out, 0);
    out.insert(out.end(), e.name.begin(), e.name.end());
    out.insert(out.end(), e.data.begin(), e.data.end());
    put32(dir, 0x02014b50);
    put16(dir, 20);
    put16(dir, 20);
    put16(dir, 0);
    put16(dir, 0);  // stored
    put32(dir, 0);
    put32(dir, crc32(e.data));
    put32(dir, e.data.size());
    put32(dir, e.data.size());
    put16(dir, e.name.size());
    put16(dir, e.extra.size());
    put16(dir, e.comment.size());
    put16(dir, 0);
    put16(dir, 0);
    put32(dir, 0);
    put32(dir, local);
    dir.insert(dir.end(), e.name.begin(), e.name.end());
    dir.insert(dir.end(), e.extra.begin(), e.extra.end());
    dir.insert(dir.end(), e.comment.begin(), e.comment.end());
  }
  const uint32_t dirAt = static_cast<uint32_t>(out.size());
  out.insert(out.end(), dir.begin(), dir.end());
  put32(out, 0x06054b50);
  put16(out, 0);
  put16(out, 0);
  put16(out, entries.size());
  put16(out, entries.size());
  put32(out, dir.size());
  put32(out, dirAt);
  put16(out, 0);
  return out;
}

std::string chapter(int i) {
  char name[64];
  std::snprintf(name, sizeof(name), "OEBPS/Text/chuong-%04d.xhtml", i);
  return name;
}

// The shape of the test book on the X3: 5.000 chapters, then the OPF, then the cover.
std::vector<Entry> bigBook() {
  std::vector<Entry> entries;
  entries.push_back({"mimetype", "application/epub+zip", "", ""});
  entries.push_back({"META-INF/container.xml", "<container/>", "", ""});
  for (int i = 0; i < 5000; i++) {
    // Every hundredth entry carries an extra field and a comment the walk must step over.
    const bool extras = i % 100 == 7;
    entries.push_back({chapter(i), "chuong " + std::to_string(i), extras ? std::string(40, 'x') : "",
                       extras ? "ghi chu" : ""});
  }
  entries.push_back({"OEBPS/toc.ncx", "<ncx/>", "", ""});
  entries.push_back({"OEBPS/content.opf", "<package/>", "", ""});
  entries.push_back({"OEBPS/cover.jpg", std::string(3000, 'j'), "", ""});
  return entries;
}

class Sink : public Print {
 public:
  std::string text;
  size_t write(uint8_t v) override { return write(&v, 1); }
  size_t write(const uint8_t* data, size_t n) override {
    text.append(reinterpret_cast<const char*>(data), n);
    return n;
  }
};

const std::string PATH = "/sach/khong-lo.epub";

void install(const std::vector<uint8_t>& zip, const std::string& path = PATH) {
  Storage.files[path] = std::make_shared<const std::vector<uint8_t>>(zip);
}

// What the reader does per item: a new ZipFile for its size, another for its bytes.
std::string readItem(const char* name, size_t* size = nullptr, const std::string& path = PATH) {
  size_t inflated = 0;
  if (!ZipFile(path).getInflatedFileSize(name, &inflated)) return "<missing>";
  if (size) *size = inflated;
  Sink sink;
  if (!ZipFile(path).readFileToStream(name, sink, 1024)) return "<unreadable>";
  return sink.text;
}

class ZipCentralDir : public ::testing::Test {
 protected:
  void SetUp() override {
    ESP.freeHeap = 200000;
    install(makeZip(bigBook()));
    // A lookup of another zip first, so nothing this test asks is already known.
    install(makeZip({{"a", "b", "", ""}}), "/khac.epub");
    size_t ignored = 0;
    ZipFile("/khac.epub").getInflatedFileSize("a", &ignored);
    storageCalls = {};
  }
};
}  // namespace

TEST_F(ZipCentralDir, FindsTheLastEntryInBlockReads) {
  size_t size = 0;
  storageCalls = {};
  EXPECT_TRUE(ZipFile(PATH).getInflatedFileSize("OEBPS/cover.jpg", &size));
  EXPECT_EQ(size, 3000u);
  // About 375 KB of directory: some 95 blocks of 4 KB, against some 65.000 calls one field at a time.
  EXPECT_LT(storageCalls.calls, 250u) << storageCalls.reads << " reads";
  std::printf("last of 5.005 entries: %zu locked calls, %zu reads, %zu bytes\n", storageCalls.calls,
              storageCalls.reads, storageCalls.bytes);
}

TEST_F(ZipCentralDir, AnItemLookedUpAgainIsNotWalkedAgain) {
  size_t size = 0;
  EXPECT_EQ(readItem("OEBPS/content.opf", &size), "<package/>");
  EXPECT_EQ(size, 10u);
  storageCalls = {};
  EXPECT_EQ(readItem("OEBPS/content.opf"), "<package/>");
  // The EOCD read that tells the zip is still the same one, the local header and the data.
  EXPECT_LT(storageCalls.calls, 16u);
}

TEST_F(ZipCentralDir, TheNextChapterIsFoundWhereTheLastOneEnded) {
  EXPECT_EQ(readItem(chapter(4000).c_str()), "chuong 4000");
  storageCalls = {};
  EXPECT_EQ(readItem(chapter(4001).c_str()), "chuong 4001");
  EXPECT_LT(storageCalls.calls, 24u);
  // Back to an earlier chapter wraps around, and still finds it.
  EXPECT_EQ(readItem(chapter(12).c_str()), "chuong 12");
  EXPECT_EQ(readItem(chapter(4999).c_str()), "chuong 4999");
}

TEST_F(ZipCentralDir, EveryEntryAnswersAsTheZipSays) {
  for (int i = 0; i < 5000; i += 97) EXPECT_EQ(readItem(chapter(i).c_str()), "chuong " + std::to_string(i)) << i;
  EXPECT_EQ(readItem("mimetype"), "application/epub+zip");
  EXPECT_EQ(readItem("OEBPS/toc.ncx"), "<ncx/>");
  EXPECT_EQ(readItem("OEBPS/khong-co.xhtml"), "<missing>");
}

TEST_F(ZipCentralDir, SizesOfEveryChapterInOneWalk) {
  std::deque<ZipFile::SizeTarget> targets;
  for (int i = 0; i < 5000; i++) {
    const auto name = chapter(i);
    targets.push_back({ZipFile::fnvHash64(name.data(), name.size()), static_cast<uint16_t>(name.size()),
                       static_cast<uint16_t>(i)});
  }
  std::sort(targets.begin(), targets.end(), [](const auto& a, const auto& b) {
    return a.hash < b.hash || (a.hash == b.hash && a.len < b.len);
  });
  std::deque<uint32_t> sizes(5000, 0);
  storageCalls = {};
  EXPECT_EQ(ZipFile(PATH).fillUncompressedSizes(targets, sizes), 5000);
  EXPECT_LT(storageCalls.calls, 250u);
  for (int i = 0; i < 5000; i++) ASSERT_EQ(sizes[i], ("chuong " + std::to_string(i)).size()) << i;
}

TEST_F(ZipCentralDir, EnumerationSeesEveryEntry) {
  size_t count = 0, extras = 0;
  storageCalls = {};
  EXPECT_TRUE(ZipFile(PATH).enumerateFileEntries([&](std::string_view path, uint32_t crc, uint32_t size) {
    count++;
    if (path == chapter(107)) extras += crc == crc32("chuong 107") && size == 10;
  }));
  EXPECT_EQ(count, 5005u);
  EXPECT_EQ(extras, 1u);
  EXPECT_LT(storageCalls.calls, 250u);
  ZipFile all(PATH);
  EXPECT_TRUE(all.loadAllFileStatSlims());
  size_t size = 0;
  EXPECT_TRUE(all.getInflatedFileSize("OEBPS/cover.jpg", &size));
  EXPECT_EQ(size, 3000u);
}

TEST_F(ZipCentralDir, ANameTooLongIsSteppedOver) {
  auto entries = bigBook();
  entries.insert(entries.begin() + 3, {std::string(300, 'n'), "dai", "", ""});
  install(makeZip(entries));
  EXPECT_EQ(readItem("OEBPS/cover.jpg").size(), 3000u);
  EXPECT_EQ(readItem(chapter(3).c_str()), "chuong 3");
}

TEST_F(ZipCentralDir, LowHeapStillFinds) {
  // A 1 KB block, then the smallest one on the stack; each zip under its own name, so it is walked.
  const auto zip = makeZip(bigBook());
  for (const uint32_t heap : {9000u, 2000u}) {
    const std::string path = "/it-heap-" + std::to_string(heap) + ".epub";
    install(zip, path);
    ESP.freeHeap = heap;
    storageCalls = {};
    EXPECT_EQ(readItem("OEBPS/content.opf", nullptr, path), "<package/>") << heap;
    EXPECT_GT(storageCalls.reads, 100u) << heap;
    EXPECT_EQ(readItem(chapter(2500).c_str(), nullptr, path), "chuong 2500") << heap;
  }
}

TEST_F(ZipCentralDir, AReplacedBookIsWalkedAgain) {
  EXPECT_EQ(readItem("OEBPS/content.opf"), "<package/>");
  auto entries = bigBook();
  entries.insert(entries.begin() + 2, {"OEBPS/moi.css", "p{}", "", ""});
  entries[entries.size() - 2].data = "<package moi/>";
  install(makeZip(entries));
  EXPECT_EQ(readItem("OEBPS/content.opf"), "<package moi/>");
}

TEST_F(ZipCentralDir, DamagedZipsAnswerMissing) {
  // Each damaged zip under a name of its own: a lookup remembered for a zip holds for its path,
  // size and directory, which damage inside the directory leaves as they were.
  const auto good = makeZip(bigBook());
  int n = 0;
  auto lookup = [&](const std::vector<uint8_t>& zip, const std::vector<const char*>& names) {
    const std::string path = "/hong-" + std::to_string(n++) + ".epub";
    install(zip, path);
    std::vector<std::string> answers;
    for (const char* name : names) answers.push_back(readItem(name, nullptr, path));
    return answers;
  };
  using V = std::vector<std::string>;
  // Cut inside the directory: the EOCD is gone, so the zip is not read at all.
  EXPECT_EQ(lookup(std::vector<uint8_t>(good.begin(), good.begin() + good.size() - 5000), {"OEBPS/cover.jpg"}),
            V{"<missing>"});
  // A directory entry whose signature is broken ends the walk there.
  auto broken = good;
  const uint32_t dirAt = broken[broken.size() - 6] | broken[broken.size() - 5] << 8 |
                         broken[broken.size() - 4] << 16 | static_cast<uint32_t>(broken[broken.size() - 3]) << 24;
  size_t at = dirAt;
  for (int i = 0; i < 2600; i++) at += 46 + (broken[at + 28] | broken[at + 29] << 8) + (broken[at + 30] | broken[at + 31] << 8) +
                                       (broken[at + 32] | broken[at + 33] << 8);
  broken[at] = 'X';
  const auto hundred = chapter(100);
  EXPECT_EQ(lookup(broken, {hundred.c_str(), "OEBPS/cover.jpg"}), (V{"chuong 100", "<missing>"}));
  // A name length that runs past the end of the file.
  auto runaway = good;
  runaway[dirAt + 28] = 0xFF;
  runaway[dirAt + 29] = 0x7F;
  EXPECT_EQ(lookup(runaway, {"OEBPS/cover.jpg"}), V{"<missing>"});
  // ZIP64 keeps the directory offset in its own record and puts 0xFFFFFFFF in the classic one; the
  // firmware reads the classic record only, so such a book answers missing instead of misreading.
  auto zip64 = good;
  for (int i = 1; i <= 4; i++) zip64[zip64.size() - 2 - i] = 0xFF;
  EXPECT_EQ(lookup(zip64, {"OEBPS/cover.jpg"}), V{"<missing>"});
  // An empty file and a file too small for an EOCD.
  EXPECT_EQ(lookup({}, {"mimetype"}), V{"<missing>"});
  EXPECT_EQ(lookup(std::vector<uint8_t>(10, 0), {"mimetype"}), V{"<missing>"});
}
