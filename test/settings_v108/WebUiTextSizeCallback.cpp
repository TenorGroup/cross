#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <utility>

struct CrossPointWebServer {
  std::function<bool(uint8_t)> uiTextSizeApplier;
  void setUiTextSizeApplier(std::function<bool(uint8_t)> applier);
  bool applyUiTextSizeSetting(uint8_t value);
};

#include "WebUiTextSizeMethods.inc"

int main() {
  CrossPointWebServer server;
  int calls = 0;
  uint8_t applied = 255;
  if (server.applyUiTextSizeSetting(1)) return EXIT_FAILURE;

  server.setUiTextSizeApplier([&](const uint8_t value) {
    ++calls;
    applied = value;
    return true;
  });
  if (!server.applyUiTextSizeSetting(2) || calls != 1 || applied != 2) return EXIT_FAILURE;

  server.setUiTextSizeApplier([&](const uint8_t value) {
    ++calls;
    applied = value;
    return false;
  });
  if (server.applyUiTextSizeSetting(1) || calls != 2 || applied != 1) return EXIT_FAILURE;

  std::puts("3 web callback scenarios, 0 failures");
  return EXIT_SUCCESS;
}
