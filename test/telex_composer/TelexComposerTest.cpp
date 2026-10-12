#include "activities/util/TelexComposer.h"

#include <chrono>
#include <iostream>
#include <string>

static void type(telex::State& state, std::string& text, size_t& cursor, char c, size_t limit = 0) {
  if (c == '<') {
    state = {};
    if (!cursor) return;
    size_t p = cursor - 1;
    while (p && (static_cast<unsigned char>(text[p]) & 0xC0) == 0x80) --p;
    text.erase(p, cursor - p);
    cursor = p;
    return;
  }
  if (telex::insert(state, text, cursor, c, limit)) return;
  if (!limit || text.size() + 1 <= limit) { text.insert(cursor, 1, c); ++cursor; }
}

int main(int argc, char**) {
  telex::State state;
  if (argc > 1) {
    constexpr const char* keys = "Nguwowif Thieen DDuowngf quyeern giuwax khuya thuowr ";
    std::string text;
    text.reserve(256);
    const auto begin = std::chrono::steady_clock::now();
    size_t keysCount = 0;
    for (int n = 0; n < 10000; ++n) {
      text.clear();
      state = {};
      size_t cursor = 0;
      for (const char* key = keys; *key; ++key) { type(state, text, cursor, *key); ++keysCount; }
    }
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - begin).count();
    std::cout << "keys=" << keysCount << " ms_per_key=" << static_cast<double>(ns) / keysCount / 1000000
              << " text_capacity=" << text.capacity() << '\n';
    return 0;
  }
  std::string raw;
  while (std::getline(std::cin, raw)) {
    std::string text;
    state = {};
    size_t cursor = 0;
    for (char c : raw) type(state, text, cursor, c);
    std::cout << text << '\n';
  }
  std::string text = "Thiên Đường";
  size_t cursor = std::string("Thiên").size();
  type(state, text, cursor, 's');
  if (text != "Thiến Đường") return 2;
  text = "a"; cursor = 1;
  type(state, text, cursor, 'j', 2);
  if (text != "a" || cursor != 1) return 3;
  type(state, text, cursor, 's', 2);
  if (text != "á" || cursor != 2) return 4;
  type(state, text, cursor, 'f', 2);
  if (text != "à" || cursor != 2) return 5;
  text = "x" + std::string(128, 'a'); cursor = text.size();
  type(state, text, cursor, 's');
  if (text != "x" + std::string(128, 'a') + "s") return 6;
  text = "中文 a"; cursor = text.size();
  type(state, text, cursor, 's');
  if (text != "中文 á") return 7;
  return 0;
}
