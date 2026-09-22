#define main measure_dataset_main
#include "measure.cpp"
#undef main

#include <map>

namespace {
unsigned checks = 0, failures = 0;

void check(bool ok, const std::string& message) {
  ++checks;
  if (!ok) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

std::map<std::string, std::string> snapshot() {
  std::map<std::string, std::string> files;
  for (const auto& item : std::filesystem::recursive_directory_iterator(fixtureRoot)) {
    if (!item.is_regular_file()) continue;
    std::ifstream input(item.path(), std::ios::binary);
    files[item.path().string()] = {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
  }
  return files;
}

void seed() {
  fixture(1);
  JsonDocument doc;
  doc["schema"] = 3;
  check(READING_STATS.fromJson(doc.as<JsonVariantConst>()), "reset memory");
  READING_STATS.statisticsReadable = true;
}

std::string document(const std::string& schema) {
  return "{" + (schema.empty() ? "" : "\"schema\":" + schema + ",") +
         "\"futureField\":\"retain literal bytes\",\"ngay\":[[20260913,12,30]]}";
}

void refused(const std::string& schema, bool inBackup) {
  seed();
  const std::string main = ReadingStatsStore::getFilePath();
  const std::string label = schema + (inBackup ? " backup" : " main");
  check(Storage.writeFile(main.c_str(), inBackup ? "{broken" : document(schema)), "seed main");
  check(Storage.writeFile((main + ".bak").c_str(), document(inBackup ? schema : "4")), "seed backup");
  const auto original = snapshot();
  for (unsigned boot = 0; boot < 2; ++boot) {
    check(!READING_STATS.loadFromFile(), label + " refuses load");
    check(!READING_STATS.statisticsReadable, label + " marks unreadable");
    check(!READING_STATS.saveToFile(), label + " refuses first save");
    check(!READING_STATS.saveToFile(), label + " refuses second save");
    check(READING_STATS.resetStatistics(false) == ReadingStatsStore::ResetResult::Failed,
          label + " refuses habits reset");
    check(READING_STATS.resetStatistics(true) == ReadingStatsStore::ResetResult::Failed,
          label + " refuses all reset");
    check(snapshot() == original, label + " preserves every file byte across load/save/reset/reload");
  }
}

void accepted(const std::string& schema) {
  seed();
  check(Storage.writeFile(ReadingStatsStore::getFilePath(), document(schema)), "seed supported schema");
  check(READING_STATS.loadFromFile(), schema + " accepts load");
  check(READING_STATS.saveToFile(), schema + " accepts save");
  check(READING_STATS.loadFromFile(), schema + " accepts reload");
  JsonDocument saved;
  READING_STATS.toJson(saved);
  check(saved["ngay"][0][0].as<unsigned>() == 20260913 && saved["ngay"][0][1].as<unsigned>() == 12 &&
            saved["ngay"][0][2].as<unsigned>() == 30,
        schema + " retains counters");
}
}  // namespace

#ifndef STATS_SCHEMA_HELPERS_ONLY
int main() {
  for (const auto* schema : {"5", "5.0", "4.0", "4294967296", "-1", "\"5\"", "\"4\"", "null", "true", "{}", "[]"}) {
    refused(schema, false);
    refused(schema, true);
  }
  for (const auto* schema : {"", "0", "1", "2", "3", "4"}) accepted(schema);
  std::filesystem::remove_all(fixtureRoot);
  std::cout << "RESULT " << checks - failures << '/' << checks << " assertions passed\n";
  return failures ? 1 : 0;
}
#endif
