struct CheckFailure : std::runtime_error { using std::runtime_error::runtime_error; };
#define CHECK(value) do { if (!(value)) throw CheckFailure(std::string(#value) + " at line " + std::to_string(__LINE__)); } while (false)

static void book(const std::string& text = "one\ntwo\nthree\nfour\nfive\nsix\n") {
  Storage = {};
  Storage.put("/book.txt", text);
}
// The body of the HTML a TXT book is streamed as: what follows the title block, up to the closing tags.
static std::string bodyOf(const Print& out) {
  const std::string html(out.bytes.begin(), out.bytes.end());
  const std::string open = "<body>\n", close = "\n</body>";
  const auto start = html.find(open), end = html.rfind(close);
  CHECK(start != std::string::npos && end != std::string::npos);
  return html.substr(start + open.size(), end - start - open.size());
}

// A TXT book goes through the EPUB pipeline as HTML (Txt::streamTxtToHtml, D17): a failed read must fail the
// conversion, and a source that arrives in pieces must come out whole.
static void txtReadError() {
  book("abcdefgh");
  Storage.files.at("/book.txt")->readPlan = {-1};
  Print out; out.allowedSpan = 1 << 20;
  CHECK(!Txt::streamTxtToHtml("/book.txt", out));
}
static void txtShortThenError() {
  book("abcdefgh"); Storage.files.at("/book.txt")->readPlan = {3, -1};
  Print out; out.allowedSpan = 1 << 20;
  CHECK(!Txt::streamTxtToHtml("/book.txt", out));
}
static void txtShortCompletes() {
  book("abc & <d>\nefgh");
  Storage.files.at("/book.txt")->readPlan = {3, 2, 3};
  Print out; out.allowedSpan = 1 << 20;
  CHECK(Txt::streamTxtToHtml("/book.txt", out));
  CHECK(bodyOf(out) == "abc &amp; &lt;d&gt;<br />efgh");
}
static void txtMissingFile() {
  book("abcdefgh");
  Print out; out.allowedSpan = 1 << 20;
  CHECK(!Txt::streamTxtToHtml("/missing.txt", out));
  CHECK(out.bytes.empty());
}
static void txtSinkFailure() {
  book("abcdefgh");
  Print out; out.allowedSpan = 4;  // the sink refuses the converter's 8 KB block: a card that is full
  CHECK(!Txt::streamTxtToHtml("/book.txt", out));
}
static void zipReadError() {
  book("abcdefgh");
  ZipFile zip; zip.file.data = Storage.files.at("/book.txt");
  zip.file.data->readPlan = {-1}; zip.stat.uncompressedSize = 8;
  Print out; out.allowedSpan = 4;
  CHECK(!zip.readFileToStream("book", out, 4));
  CHECK(out.maxSpan <= 4);
}
static void zipShortCompletes() {
  book("abcdefgh");
  ZipFile zip; zip.file.data = Storage.files.at("/book.txt");
  zip.file.data->readPlan = {2, 1, 4, 1}; zip.stat.uncompressedSize = 8;
  Print out; out.allowedSpan = 4;
  CHECK(zip.readFileToStream("book", out, 4));
  CHECK(std::string(out.bytes.begin(), out.bytes.end()) == "abcdefgh");
}
static void zipEarlyStop() {
  book("abcdefgh");
  ZipFile zip; zip.file.data = Storage.files.at("/book.txt"); zip.stat.uncompressedSize = 8;
  Print out; out.limit = 2; CHECK(zip.readFileToStream("book", out, 4, true));
  zip.file.pos = 0; CHECK(!zip.readFileToStream("book", out, 4, false));
}
static void zipReadZero() {
  book("abcdefgh");
  ZipFile zip; zip.file.data = Storage.files.at("/book.txt");
  zip.file.data->readPlan = {0}; zip.stat.uncompressedSize = 8;
  Print out; out.allowedSpan = 4;
  CHECK(!zip.readFileToStream("book", out, 4));
  CHECK(out.bytes.empty());
}
static void zipShortThenError() {
  book("abcdefgh");
  ZipFile zip; zip.file.data = Storage.files.at("/book.txt");
  zip.file.data->readPlan = {2, -1}; zip.stat.uncompressedSize = 8;
  Print out; out.allowedSpan = 4;
  CHECK(!zip.readFileToStream("book", out, 4, true));
  CHECK(out.maxSpan <= 4);
}
static void zipInflateErrorEarlyStop() {
  book("abcdefgh");
  ZipFile zip; zip.file.data = Storage.files.at("/book.txt");
  zip.stat = {ZIP_METHOD_DEFLATED, 8, 8, 0};
  Print out; out.allowedSpan = 4; out.limit = 2;
  InflateStream::enabled = true; InflateStream::configuredProduced = 4;
  InflateStream::configuredStatus = InflateStream::Status::Error;
  CHECK(!zip.readFileToStream("book", out, 4, true));
  InflateStream::enabled = false;
}
static void readSeekFailure() {
  book("abcdefgh"); Storage.files.at("/book.txt")->seekOk = false;
  ZipFile zip; zip.file.data = Storage.files.at("/book.txt"); zip.stat.uncompressedSize = 8;
  Print out; CHECK(!zip.readFileToStream("book", out, 4)); CHECK(out.bytes.empty());
}
static void zipZeroChunk() {
  book("abcdefgh"); ZipFile zip; zip.file.data = Storage.files.at("/book.txt");
  zip.stat.uncompressedSize = 8; Print out;
  CHECK(!zip.readFileToStream("book", out, 0)); CHECK(out.bytes.empty());
}

int main(int argc, char** argv) {
  const std::string filter = argc > 1 ? argv[1] : "all";
  int total = 0, failures = 0;
  const std::vector<std::pair<const char*, std::function<void()>>> cases = {
    {"READ-03 negative read", txtReadError}, {"READ-03 short reads complete", txtShortCompletes},
    {"READ-03 missing file", txtMissingFile}, {"READ-03 refused output", txtSinkFailure},
    {"IO-01 negative stored read", zipReadError},
    {"IO-01 short reads complete", zipShortCompletes}, {"IO-01 early stop contract", zipEarlyStop},
    {"IO-01 zero read", zipReadZero}, {"IO-01 partial then read error preserves failure", zipShortThenError},
    {"IO-01 inflate error retains failure before early stop", zipInflateErrorEarlyStop},
    {"READ-03 partial then read error", txtShortThenError},
    {"IO-01 failed seek", readSeekFailure}, {"IO-01 zero chunk", zipZeroChunk},
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
