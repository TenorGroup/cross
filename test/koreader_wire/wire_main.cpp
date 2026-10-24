// What KOReaderSyncClient puts on the wire for the configurations a v1.0.76 device can hold. The same driver is
// built against the v1.0.76 sources to make golden-v1.0.76.txt and against this tree to be compared with it.
#include <ArduinoJson.h>
#include <HalMemory.h>
#include <KOReaderCredentialStore.h>
#include <KOReaderSyncClient.h>
#include <SecureHttpClient.h>

#include <cstdio>
#include <string>

EspClass ESP;
unsigned long millis() { return 0; }
void delay(unsigned long) {}
HalMemory::HeapStats HalMemory::getDefaultHeap() { return {200000, 300000, 150000, 100000}; }

namespace {
KOReaderProgress sample() {
  KOReaderProgress p;
  p.document = "hash";
  p.progress = "/body/DocFragment[3]/body/p[2]/text().4";
  p.percentage = 0.5f;
  KOReaderRichPosition pos;
  pos.pctQ = 500000;
  pos.spineIndex = 2;
  pos.pageNumber = 1;
  pos.totalPages = 4;
  pos.xpath = p.progress;
  p.position = pos;
  KOReaderMetadata meta;
  meta.filename = "book.epub";
  meta.title = "Title";
  meta.authors = "Author";
  p.metadata = meta;
  return p;
}

void run(const char* name, const char* json, const char* newUrl = nullptr) {
  std::printf("=== %s\n", name);
  JsonDocument doc;
  deserializeJson(doc, json);
  auto& store = KOReaderCredentialStore::getInstance();
  store.fromJson(doc.as<JsonVariantConst>());
  if (newUrl) store.setServerUrl(newUrl);
  wire::transcript.clear();
  KOReaderSyncClient::authenticate();
  KOReaderSyncClient::createUser();
  KOReaderProgress got;
  KOReaderSyncClient::getProgress("hash", got);
  std::printf("got position=%d\n", got.position.has_value() ? 1 : 0);
  KOReaderSyncClient::updateProgress(sample());
  for (const auto& t : wire::transcript) std::printf("%s", t.c_str());
}
}  // namespace

int main() {
  const char* base = R"({"cfgVersion":3,"username":"u","password":"p","serverUrl":"%s","matchMethod":0,"sendMetadata":%s,"syncBehavior":1,"autoSync":true})";
  char buf[512];
  struct Case { const char* name; const char* url; const char* meta; };
  const Case cases[] = {
      {"default server, metadata off", "", "false"},
      {"default server, metadata on", "", "true"},
      {"tenor server written out", "https://kosync.tenor.vn", "false"},
      {"tenor server, trailing slash", "https://kosync.tenor.vn/", "true"},
      {"crosspoint server", "https://sync.crosspointreader.com", "false"},
      {"custom http server", "http://192.168.1.5:8080", "true"},
      {"custom https server", "https://sync.koreader.rocks:443", "false"},
      {"url without scheme", "kosync.tenor.vn", "false"},
  };
  for (const auto& c : cases) {
    std::snprintf(buf, sizeof(buf), base, c.url, c.meta);
    run(c.name, buf);
  }
  // An old file with no version and no URL was syncing against the old default: it stays there.
  run("pre-v2 file", R"({"username":"u","password":"p","matchMethod":0,"sendMetadata":false,"syncBehavior":1})");
  // The URL typed in the settings screen after the file was read.
  std::snprintf(buf, sizeof(buf), base, "", "false");
  run("url typed: custom", buf, "https://other.example");
  run("url typed: back to tenor", R"({"cfgVersion":3,"username":"u","password":"p","serverUrl":"https://other.example","sendMetadata":false,"syncBehavior":1})", "https://kosync.tenor.vn");
  return 0;
}
