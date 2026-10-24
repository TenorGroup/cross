struct CheckFailure : std::runtime_error { using std::runtime_error::runtime_error; };
#define CHECK(value) do { if (!(value)) throw CheckFailure(std::string(#value) + " at line " + std::to_string(__LINE__)); } while (false)

static std::unique_ptr<Txt> book(const std::string& text = "one\ntwo\nthree\nfour\nfive\nsix\n") {
  Storage = {};
  Storage.put("/book.txt", text);
  auto txt = std::make_unique<Txt>(); txt->fileSize = text.size(); return txt;
}

static void txtReadError() {
  auto txt = book("abcdefgh");
  Storage.files.at("/book.txt")->readPlan = {-1};
  uint8_t data[8] = {};
  CHECK(!txt->readContent(data, 0, sizeof(data)));
}
static void txtShortEof() {
  auto txt = book("abcdefgh");
  Storage.files.at("/book.txt")->readPlan = {3, 0};
  uint8_t data[8] = {};
  CHECK(!txt->readContent(data, 0, sizeof(data)));
}
static void txtShortCompletes() {
  auto txt = book("abcdefgh");
  Storage.files.at("/book.txt")->readPlan = {3, 2, 3};
  uint8_t data[8] = {};
  CHECK(txt->readContent(data, 0, sizeof(data)));
  CHECK(std::memcmp(data, "abcdefgh", 8) == 0);
}
static void zipReadError() {
  auto txt = book("abcdefgh");
  ZipFile zip; zip.file.data = Storage.files.at("/book.txt");
  zip.file.data->readPlan = {-1}; zip.stat.uncompressedSize = 8;
  Print out; out.allowedSpan = 4;
  CHECK(!zip.readFileToStream("book", out, 4));
  CHECK(out.maxSpan <= 4);
}
static void zipShortCompletes() {
  auto txt = book("abcdefgh");
  ZipFile zip; zip.file.data = Storage.files.at("/book.txt");
  zip.file.data->readPlan = {2, 1, 4, 1}; zip.stat.uncompressedSize = 8;
  Print out; out.allowedSpan = 4;
  CHECK(zip.readFileToStream("book", out, 4));
  CHECK(std::string(out.bytes.begin(), out.bytes.end()) == "abcdefgh");
}
static void zipEarlyStop() {
  auto txt = book("abcdefgh");
  ZipFile zip; zip.file.data = Storage.files.at("/book.txt"); zip.stat.uncompressedSize = 8;
  Print out; out.limit = 2; CHECK(zip.readFileToStream("book", out, 4, true));
  zip.file.pos = 0; CHECK(!zip.readFileToStream("book", out, 4, false));
}
static void zipReadZero() {
  auto txt = book("abcdefgh");
  ZipFile zip; zip.file.data = Storage.files.at("/book.txt");
  zip.file.data->readPlan = {0}; zip.stat.uncompressedSize = 8;
  Print out; out.allowedSpan = 4;
  CHECK(!zip.readFileToStream("book", out, 4));
  CHECK(out.bytes.empty());
}
static void zipShortThenError() {
  auto txt = book("abcdefgh");
  ZipFile zip; zip.file.data = Storage.files.at("/book.txt");
  zip.file.data->readPlan = {2, -1}; zip.stat.uncompressedSize = 8;
  Print out; out.allowedSpan = 4;
  CHECK(!zip.readFileToStream("book", out, 4, true));
  CHECK(out.maxSpan <= 4);
}
static void zipInflateErrorEarlyStop() {
  auto txt = book("abcdefgh");
  ZipFile zip; zip.file.data = Storage.files.at("/book.txt");
  zip.stat = {ZIP_METHOD_DEFLATED, 8, 8, 0};
  Print out; out.allowedSpan = 4; out.limit = 2;
  InflateStream::enabled = true; InflateStream::configuredProduced = 4;
  InflateStream::configuredStatus = InflateStream::Status::Error;
  CHECK(!zip.readFileToStream("book", out, 4, true));
  InflateStream::enabled = false;
}
static void txtReadZero() {
  auto txt = book("abcdefgh"); Storage.files.at("/book.txt")->readPlan = {0};
  uint8_t bytes[8] = {}; CHECK(!txt->readContent(bytes, 0, sizeof(bytes)));
}
static void txtShortThenError() {
  auto txt = book("abcdefgh"); Storage.files.at("/book.txt")->readPlan = {3, -1};
  uint8_t bytes[8] = {}; CHECK(!txt->readContent(bytes, 0, sizeof(bytes)));
}
static void readSeekFailure() {
  auto txt = book("abcdefgh"); Storage.files.at("/book.txt")->seekOk = false;
  uint8_t bytes[8] = {}; CHECK(!txt->readContent(bytes, 0, sizeof(bytes)));
  ZipFile zip; zip.file.data = Storage.files.at("/book.txt"); zip.stat.uncompressedSize = 8;
  Print out; CHECK(!zip.readFileToStream("book", out, 4)); CHECK(out.bytes.empty());
}
static void zipZeroChunk() {
  auto txt = book("abcdefgh"); ZipFile zip; zip.file.data = Storage.files.at("/book.txt");
  zip.stat.uncompressedSize = 8; Print out;
  CHECK(!zip.readFileToStream("book", out, 0)); CHECK(out.bytes.empty());
}
static void txtRangeBounds() {
  auto txt = book("abcdefgh"); uint8_t bytes[8] = {};
  CHECK(!txt->readContent(bytes, std::numeric_limits<size_t>::max(), 4));
  CHECK(!txt->readContent(bytes, 2, std::numeric_limits<size_t>::max()));
  CHECK(!txt->readContent(nullptr, 0, 1));
  CHECK(txt->readContent(nullptr, 8, 0));
}


int main(int argc, char** argv) {
  const std::string filter = argc > 1 ? argv[1] : "all";
  int total = 0, failures = 0;
  const std::vector<std::pair<const char*, std::function<void()>>> cases = {
    {"READ-03 negative read", txtReadError}, {"READ-03 short then EOF", txtShortEof},
    {"READ-03 short reads complete", txtShortCompletes}, {"IO-01 negative stored read", zipReadError},
    {"IO-01 short reads complete", zipShortCompletes}, {"IO-01 early stop contract", zipEarlyStop},
    {"IO-01 zero read", zipReadZero}, {"IO-01 partial then read error preserves failure", zipShortThenError},
    {"IO-01 inflate error retains failure before early stop", zipInflateErrorEarlyStop},
    {"READ-03 zero read", txtReadZero}, {"READ-03 partial then read error", txtShortThenError},
    {"IO-01 READ-03 failed seek", readSeekFailure}, {"IO-01 zero chunk", zipZeroChunk},
    {"READ-03 requested range bounds", txtRangeBounds},
  };
  for (const auto& test : cases) {
    if (filter != "all" && std::string(test.first).find(filter) == std::string::npos) continue;
    ++total;
    InflateStream::enabled = false;
    try { test.second(); std::cout << "PASS " << test.first << '\n'; }
    catch (const std::exception& error) { ++failures; std::cout << "FAIL " << test.first << ": " << error.what() << '\n'; }
  }
  std::cout << "RESULT " << total - failures << "/" << total << " passed\n";
  return failures || total == 0 ? 1 : 0;
}
