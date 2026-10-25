// What a section build does with what an earlier build left on the card, and with a font size the font system
// turned down. Built for a board with PSRAM (a heading has a size of its own there).
//
// fixtures/v70 is the cache of one chapter as the firmware at 8c26ea97 (section version 70) wrote it; see
// fixtures/v70/gen_v70.cpp. The firmware at section version 71 opened it on a X4C and never showed the book.
#include <Arduino.h>
#include <Epub/Page.h>
#include <Epub/Section.h>
#include <GfxRenderer.h>
#include <TxtToHtml.h>
#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {
namespace fs = std::filesystem;

std::string readAll(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void writeAll(const fs::path& path, const std::string& data) {
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(data.data(), static_cast<std::streamsize>(data.size()));
}

// The font system's answer for any size but the book's: refused for want of memory, or no size of its own.
bool heapIsShort = false;
int answerSizes(void*, int, float) { return heapIsShort ? FontVariantProvider::REFUSED : 0; }

class SectionRestartTest : public ::testing::Test {
 protected:
  static constexpr int SPINE = 1;
  fs::path root, coldRoot;
  std::shared_ptr<Epub> epub = std::make_shared<Epub>();
  GfxRenderer renderer;
  ReaderRenderSpec spec;
  std::string html;

  fs::path bin() const { return root / "sections/1.bin"; }
  fs::path sidecar(const char* name) const { return root / "sections" / name; }

  void SetUp() override {
    heapIsShort = false;
    renderer.setFontVariantProvider({nullptr, answerSizes});
    const std::string stem = "cross-restart-" + std::to_string(getpid()) + "-" +
                             ::testing::UnitTest::GetInstance()->current_test_info()->name();
    root = fs::temp_directory_path() / stem;
    coldRoot = fs::temp_directory_path() / (stem + "-cold");
    fs::remove_all(root);
    fs::remove_all(coldRoot);
    fs::create_directories(root);
    spec.viewportWidth = 160;
    spec.viewportHeight = 34;
    spec.embeddedStyle = false;
    spec.dropCapMode = 0;
    html = readAll(fs::path(FIXTURES) / "v70/html/1.html");
    ASSERT_FALSE(html.empty());
    epub->cachePath = root.string();
    epub->contents = html;
  }
  void TearDown() override {
    fs::remove_all(root);
    fs::remove_all(coldRoot);
  }

  void installV70() { fs::copy(fs::path(FIXTURES) / "v70", root, fs::copy_options::recursive); }

  // The chapter laid out in one pass from nothing, under the same spec and the same answers.
  std::string cold(const std::string& chapterHtml) {
    auto coldEpub = std::make_shared<Epub>();
    coldEpub->cachePath = coldRoot.string();
    coldEpub->contents = chapterHtml;
    fs::remove_all(coldRoot);
    fs::create_directories(coldRoot);
    Section section(coldEpub, SPINE, renderer);
    EXPECT_TRUE(section.createSectionFile(spec));
    return readAll(coldRoot / "sections/1.bin");
  }

  // Parked and woken every `pages` pages, as a reader with a radio up builds.
  void buildInChunks(Section& section, const int pages) {
    int guard = 0;
    while (!section.isBuildComplete() && ++guard < 4000) {
      ASSERT_TRUE(section.buildSomeMore(pages)) << "the build failed after " << section.pageCount << " pages";
      if (!section.isBuildComplete() && section.isBuilding()) ASSERT_TRUE(section.parkBuild());
    }
    ASSERT_TRUE(section.isBuildComplete());
  }

  // A checkpoint is: "1KPC", byte count, CRC-32 of the payload, then the payload (source size, offset, pages, the
  // prolog and open ancestors the resume replays as a string with a 4-byte length, the layout state). It ends the file.
  static constexpr size_t CHECKPOINT_PAYLOAD = 12, PREFIX_LENGTH_AT = 10;
  static uint32_t crc32(const std::string& data, const size_t from, const size_t count) {
    uint32_t crc = UINT32_MAX;
    for (size_t i = from; i < from + count; ++i) {
      crc ^= static_cast<uint8_t>(data[i]);
      for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
  }
  // The checkpoint in `file` fails its check (a byte of the ancestors changed, the CRC left alone).
  static bool damageCheckpoint(std::string& file) {
    const auto at = file.rfind("1KPC");
    if (at == std::string::npos) return false;
    file[at + CHECKPOINT_PAYLOAD + PREFIX_LENGTH_AT + 4 + 2] ^= 0x20;
    return true;
  }
  // The checkpoint in `file` is whole and restores, but one more element is open than the chapter has: the resumed
  // parse runs into the end of <body> and Expat stops on a mismatched tag, far from where the resume was made.
  static bool openOneMoreAncestor(std::string& file) {
    const auto at = file.rfind("1KPC");
    if (at == std::string::npos) return false;
    const size_t payload = at + CHECKPOINT_PAYLOAD;
    uint32_t bytes = 0, length = 0;
    std::memcpy(&bytes, &file[at + 4], 4);
    std::memcpy(&length, &file[payload + PREFIX_LENGTH_AT], 4);
    file.insert(payload + PREFIX_LENGTH_AT + 4 + length, "<div>");
    length += 5;
    bytes += 5;
    std::memcpy(&file[payload + PREFIX_LENGTH_AT], &length, 4);
    std::memcpy(&file[at + 4], &bytes, 4);
    const uint32_t crc = crc32(file, payload, bytes);
    std::memcpy(&file[at + 8], &crc, 4);
    return true;
  }

  // A chapter with a heading after `before` paragraphs.
  static std::string chapterWithHeadingAfter(const int before) {
    std::string c =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<html xmlns=\"http://www.w3.org/1999/xhtml\"><head><title>T</title>"
        "</head><body>";
    for (int i = 0; i < 90; ++i) {
      if (i == before) c += "<h2>Section title</h2>";
      c += "<p>Paragraph " + std::to_string(i) + " alpha beta gamma delta epsilon zeta eta theta iota kappa lambda.</p>";
    }
    return c + "</body></html>";
  }
};

// ---- What an earlier version left: none of it may reach the new build ----------------------------------------------

// The files the firmware at version 70 leaves for a chapter (a partial .bin, its resume points, a crashed build's
// staging files) are not used by a build at 71: the chapter comes out as a cold build lays it out, byte for byte.
TEST_F(SectionRestartTest, AChapterCachedByVersion70IsRebuiltByteForByteAsAColdBuild) {
  const std::string want = cold(html);
  installV70();
  ASSERT_EQ(static_cast<uint8_t>(readAll(bin())[0]), 212) << "the fixture is no longer a version 70 partial";
  Section section(epub, SPINE, renderer);
  ASSERT_FALSE(section.loadSectionFile(spec));
  ASSERT_FALSE(section.isPartial());
  ASSERT_TRUE(section.startBuild(spec));
  ASSERT_TRUE(section.buildSomeMore(0)) << "the build of the chapter failed";
  ASSERT_TRUE(section.isBuildComplete());
  EXPECT_EQ(readAll(bin()), want);
  for (const char* left : {"1.bin.part", "1.bin.lut.part", "1.bin.checkpoint.part", "1.dd.part"})
    EXPECT_FALSE(fs::exists(sidecar(left))) << left << " outlived the build";
  Section reopened(epub, SPINE, renderer);
  EXPECT_TRUE(reopened.loadSectionFile(spec));
  EXPECT_FALSE(reopened.isPartial());
}

TEST_F(SectionRestartTest, AChapterCachedByVersion70IsRebuiltInParkedChunksAsAColdBuild) {
  const std::string want = cold(html);
  installV70();
  Section section(epub, SPINE, renderer);
  ASSERT_FALSE(section.loadSectionFile(spec));
  ASSERT_TRUE(section.startBuild(spec));
  buildInChunks(section, 3);
  EXPECT_EQ(readAll(bin()), want);
}

// ---- A font size turned down for want of memory is a build short of heap, not a broken chapter -----------------------

// The reader frees heap and builds again when buildStarved() says so (EpubReaderActivity: releaseHeapForBuild); with
// anything else it shows the build error, and the next press hits the same refusal for as long as the heap stays so.
TEST_F(SectionRestartTest, AHeadingSizeTheFontSystemRefusedStarvesTheBuildAndItGoesOnWhenTheHeapIsBack) {
  const std::string want = cold(html);
  heapIsShort = true;
  Section section(epub, SPINE, renderer);
  ASSERT_TRUE(section.startBuild(spec));
  EXPECT_FALSE(section.buildSomeMore(0));
  EXPECT_TRUE(section.buildStarved()) << "the reader would show a build error and every press would do it again";
  EXPECT_TRUE(section.isBuilding()) << "the build was thrown away instead of kept for the retry";
  EXPECT_FALSE(section.buildSomeMore(0)) << "still short of heap";
  EXPECT_TRUE(section.buildStarved());
  heapIsShort = false;
  ASSERT_TRUE(section.buildSomeMore(0));
  ASSERT_TRUE(section.isBuildComplete());
  EXPECT_EQ(readAll(bin()), want);
}

// A reader that leaves while the build waits for heap keeps nothing of it: no staging file, the next open builds as ever.
TEST_F(SectionRestartTest, AStarvedBuildThatIsLeftBehindLeavesNoStagingFileAndTheNextOpenBuilds) {
  const std::string want = cold(html);
  heapIsShort = true;
  {
    Section section(epub, SPINE, renderer);
    ASSERT_TRUE(section.startBuild(spec));
    EXPECT_FALSE(section.buildSomeMore(0));
    ASSERT_TRUE(section.buildStarved());
  }
  for (const char* left : {"1.bin", "1.bin.part", "1.bin.lut.part", "1.bin.checkpoint.part", "1.dd.part"})
    EXPECT_FALSE(fs::exists(sidecar(left))) << left;
  heapIsShort = false;
  Section next(epub, SPINE, renderer);
  ASSERT_FALSE(next.loadSectionFile(spec));
  ASSERT_TRUE(next.startBuild(spec));
  ASSERT_TRUE(next.buildSomeMore(0));
  EXPECT_EQ(readAll(bin()), want);
}

TEST_F(SectionRestartTest, ASizeRefusedAfterPagesWereLaidOutStartsTheChapterOverWhenTheHeapIsBack) {
  const std::string chapter = chapterWithHeadingAfter(60);
  epub->contents = chapter;
  const std::string want = cold(chapter);
  Section section(epub, SPINE, renderer);
  ASSERT_TRUE(section.startBuild(spec));
  ASSERT_TRUE(section.buildSomeMore(4));
  ASSERT_GE(section.pageCount, 4);
  heapIsShort = true;
  EXPECT_FALSE(section.buildSomeMore(0));
  EXPECT_TRUE(section.buildStarved()) << "pages were laid out before the heading, the build was thrown away";
  heapIsShort = false;
  ASSERT_TRUE(section.buildSomeMore(0));
  ASSERT_TRUE(section.isBuildComplete());
  EXPECT_EQ(readAll(bin()), want);
}

// ---- A resume point that does not parse is dropped, once --------------------------------------------------------------

TEST_F(SectionRestartTest, AParseErrorAfterAParkedResumeLaysTheChapterOutAgainFromItsTop) {
  const std::string want = cold(html);
  Section section(epub, SPINE, renderer);
  ASSERT_TRUE(section.startBuild(spec));
  ASSERT_TRUE(section.buildSomeMore(3));
  ASSERT_TRUE(section.parkBuild());
  std::string checkpoint = readAll(sidecar("1.bin.checkpoint.part"));
  ASSERT_TRUE(openOneMoreAncestor(checkpoint));
  writeAll(sidecar("1.bin.checkpoint.part"), checkpoint);
  ASSERT_TRUE(section.buildSomeMore(0)) << "the resume point that led to a parse error is not dropped";
  ASSERT_TRUE(section.isBuildComplete());
  EXPECT_EQ(readAll(bin()), want);
}

TEST_F(SectionRestartTest, AParkedCheckpointThatDoesNotRestoreLaysTheChapterOutAgainFromItsTop) {
  const std::string want = cold(html);
  Section section(epub, SPINE, renderer);
  ASSERT_TRUE(section.startBuild(spec));
  ASSERT_TRUE(section.buildSomeMore(3));
  ASSERT_TRUE(section.parkBuild());
  std::string checkpoint = readAll(sidecar("1.bin.checkpoint.part"));
  ASSERT_TRUE(damageCheckpoint(checkpoint));
  writeAll(sidecar("1.bin.checkpoint.part"), checkpoint);
  ASSERT_TRUE(section.buildSomeMore(0)) << "the resume point that does not restore is not dropped";
  ASSERT_TRUE(section.isBuildComplete());
  EXPECT_EQ(readAll(bin()), want);
}

TEST_F(SectionRestartTest, AParseErrorAfterRestoringAPartialLaysTheChapterOutAgainFromItsTop) {
  const std::string want = cold(html);
  {
    Section first(epub, SPINE, renderer);
    ASSERT_TRUE(first.startBuild(spec));
    ASSERT_TRUE(first.buildSomeMore(4));
  }  // closed mid-build: the partial is kept
  std::string partial = readAll(bin());
  ASSERT_TRUE(openOneMoreAncestor(partial));
  writeAll(bin(), partial);
  Section section(epub, SPINE, renderer);
  ASSERT_TRUE(section.loadSectionFile(spec));
  ASSERT_TRUE(section.isPartial());
  ASSERT_TRUE(section.startBuild(spec));
  ASSERT_TRUE(section.buildSomeMore(0)) << "the resume point that led to a parse error is not dropped";
  ASSERT_TRUE(section.isBuildComplete());
  EXPECT_EQ(readAll(bin()), want);
}

TEST_F(SectionRestartTest, APartialWhoseCheckpointDoesNotRestoreStartsFromTheTop) {
  const std::string want = cold(html);
  {
    Section first(epub, SPINE, renderer);
    ASSERT_TRUE(first.startBuild(spec));
    ASSERT_TRUE(first.buildSomeMore(4));
  }
  std::string partial = readAll(bin());
  ASSERT_TRUE(damageCheckpoint(partial));
  writeAll(bin(), partial);
  Section section(epub, SPINE, renderer);
  ASSERT_TRUE(section.loadSectionFile(spec));
  ASSERT_TRUE(section.startBuild(spec));
  ASSERT_TRUE(section.buildSomeMore(0));
  ASSERT_TRUE(section.isBuildComplete());
  EXPECT_EQ(readAll(bin()), want);
}

// A converted TXT parks and resumes at the end of a <br />, with the state of the soft block that waits there (a bit
// the cold parser sets from the file's version comment): the chapter laid out again from the top is the cold one.
namespace {
struct HtmlSink : Print {
  std::string str;
  size_t write(uint8_t b) override {
    str.push_back(static_cast<char>(b));
    return 1;
  }
  size_t write(const uint8_t* data, size_t length) override {
    str.append(reinterpret_cast<const char*>(data), length);
    return length;
  }
};
}  // namespace

TEST_F(SectionRestartTest, AParseErrorAfterAParkedResumeOfATxtLaysItOutAgainAsTheColdOne) {
  std::string text;
  for (unsigned line = 0; line < 500; ++line) {
    for (unsigned word = 0; word < 6 + line % 30; ++word) text += "w" + std::to_string(line) + "x" + std::to_string(word) + " ";
    text += line % 9 == 4 ? "\n\n" : "\n";
  }
  HtmlSink converted;
  ASSERT_TRUE(TxtToHtml::stream("book.txt", text, converted));
  epub->contents = converted.str;
  const std::string want = cold(converted.str);
  Section section(epub, SPINE, renderer);
  ASSERT_TRUE(section.startBuild(spec));
  ASSERT_TRUE(section.buildSomeMore(6));
  ASSERT_TRUE(section.parkBuild());
  std::string checkpoint = readAll(sidecar("1.bin.checkpoint.part"));
  ASSERT_TRUE(openOneMoreAncestor(checkpoint));
  writeAll(sidecar("1.bin.checkpoint.part"), checkpoint);
  ASSERT_TRUE(section.buildSomeMore(0)) << "the resume point that led to a parse error is not dropped";
  ASSERT_TRUE(section.isBuildComplete());
  EXPECT_EQ(readAll(bin()), want);
}

// A chapter that does not parse from its top either is reported as it is today, once: the call returns, the build is
// gone and the next call has nothing to resume. (Neither loops.)
TEST_F(SectionRestartTest, AChapterThatBreaksFromItsTopToo_IsReportedOnceAndTheBuildEnds) {
  std::string broken = html;
  const auto at = broken.find("</p>", broken.size() / 2);
  ASSERT_NE(at, std::string::npos);
  broken[at + 2] = 'q';  // </q> closes a <p>
  epub->contents = broken;
  {
    Section section(epub, SPINE, renderer);
    ASSERT_TRUE(section.startBuild(spec));
    EXPECT_FALSE(section.buildSomeMore(0));
    EXPECT_FALSE(section.buildStarved());
    EXPECT_FALSE(section.isBuilding());
    EXPECT_FALSE(section.buildSomeMore(0)) << "nothing is left to resume";
  }
  fs::remove_all(root);
  fs::create_directories(root);
  Section parked(epub, SPINE, renderer);
  ASSERT_TRUE(parked.startBuild(spec));
  ASSERT_TRUE(parked.buildSomeMore(3));
  ASSERT_TRUE(parked.parkBuild());
  EXPECT_FALSE(parked.buildSomeMore(0)) << "the resume fails at the same place a cold build does";
  EXPECT_FALSE(parked.buildStarved());
  EXPECT_FALSE(parked.isBuilding());
  EXPECT_FALSE(parked.buildSomeMore(0));
}

}  // namespace
