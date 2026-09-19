#include <cstdio>
#include <cstring>

int main() {
  const char* title = "Ậ Ễ Ố đo mực thanh trạng thái";
  JsonDocument saved;
  saved["books"][0]["title"] = title;
  saved["books"][0]["path"] = "/books/do.epub";
  String output;
  serializeJson(saved, output);
  const String input = output;
  int failures = 0;
  for (int pass = 0; pass < 2; ++pass) {
    JsonDocument loaded;
    const auto error = deserializeJson(loaded, input);
    const bool complete = !error && loaded["books"].size() == 1 &&
                          strcmp(loaded["books"][0]["title"] | "", title) == 0 &&
                          strcmp(loaded["books"][0]["path"] | "", "/books/do.epub") == 0;
    std::printf("%s utf8 roundtrip %d error=%s arduino_string=%d\n",
                complete ? "PASS" : "FAIL", pass, error.c_str(), ARDUINOJSON_ENABLE_ARDUINO_STRING);
    failures += !complete;
  }
  // Re-reading a String starts at byte zero, including for ASCII-only input.
  const String ascii("{\"books\":[{\"title\":\"ASCII\"}]}");
  for (int pass = 0; pass < 2; ++pass) {
    JsonDocument loaded;
    const auto error = deserializeJson(loaded, ascii);
    const bool complete = !error && strcmp(loaded["books"][0]["title"] | "", "ASCII") == 0;
    std::printf("%s ascii reload %d error=%s\n", complete ? "PASS" : "FAIL", pass, error.c_str());
    failures += !complete;
  }
  return failures ? 1 : 0;
}
