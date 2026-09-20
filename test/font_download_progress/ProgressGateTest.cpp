#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#include "activities/settings/FontDownloadProgress.h"

namespace {

void expect(const bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

std::string readFile(const std::string& path) {
  std::ifstream stream(path);
  return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

unsigned legacyPaintCount() {
  constexpr size_t total = 240000;
  int lastPercent = -1;
  size_t lastBytes = static_cast<size_t>(-1);
  unsigned long lastUpdateMs = 0;
  unsigned paints = 0;

  for (unsigned long now = 0; now <= 10000; now += 25) {
    const size_t downloaded = now * 3;
    const int percent = static_cast<int>((static_cast<uint64_t>(downloaded) * 100) / total);
    if ((downloaded != lastBytes || percent != lastPercent) &&
        (lastPercent < 0 || percent >= 100 || percent >= lastPercent + 5 || now - lastUpdateMs >= 500)) {
      lastPercent = percent;
      lastBytes = downloaded;
      lastUpdateMs = now;
      ++paints;
    }
  }
  return paints;
}

void testOneOutstandingPaintDuringSlowRender() {
  fontdownload::ProgressRenderGate gate;
  constexpr size_t total = 240000;
  constexpr unsigned long renderDurationMs = 390;
  unsigned long renderBusyUntil = 0;
  unsigned scheduled = 0;
  unsigned inputPumps = 0;

  for (unsigned long now = 0; now <= 10000; now += 25) {
    ++inputPumps;
    if (now >= renderBusyUntil) {
      gate.renderStarted();
      const size_t downloaded = now * 3;  // 3 KB/s, close to the measured transfer.
      if (gate.requestDue(downloaded, total, now)) {
        ++scheduled;
        renderBusyUntil = now + renderDurationMs;
      }
    }
  }

  expect(inputPumps == 401, "every 25 ms poll must remain available for Back input");
  expect(scheduled <= 6, "a 10 s slow transfer must coalesce progress paints");
}

void testPercentAndTerminalUpdatesRemainVisible() {
  fontdownload::ProgressRenderGate gate;
  expect(gate.requestDue(0, 100, 0), "first snapshot must paint");
  expect(gate.renderQueued(), "first snapshot must reserve its only paint");
  expect(!gate.requestDue(20, 100, 25), "queued render must reject another notification");

  gate.renderStarted();
  expect(!gate.renderQueued(), "render start must release the queue slot");
  expect(gate.requestDue(10, 100, 100), "a ten-percent change must paint");
  gate.renderStarted();
  expect(gate.requestDue(100, 100, 125), "completion must paint even inside the interval");
}

void testCallbackContractAvoidsBlockingOnRender() {
  const char* overridePath = std::getenv("FONT_DOWNLOAD_ACTIVITY_SOURCE");
  const std::string sourcePath =
      overridePath ? overridePath : std::string(REPO_ROOT_PATH) + "/src/activities/settings/FontDownloadActivity.cpp";
  const std::string source = readFile(sourcePath);
  const size_t callback = source.find("downloadUrl_, staging,");
  const size_t callbackEnd = source.find("&cancelRequested_", callback);
  expect(callback != std::string::npos && callbackEnd != std::string::npos && callback < callbackEnd,
         "font downloader callback must be present");
  const std::string body = source.substr(callback, callbackEnd - callback);

  expect(body.find("mappedInput.update()") != std::string::npos, "callback must continue pumping input");
  expect(body.find("RenderLock lock(RenderLock::TryTake{})") != std::string::npos,
         "progress snapshot must use a nonblocking render lock");
  expect(body.find("lock.acquired()") != std::string::npos, "callback must handle a busy render lock");
  expect(body.find("progressRenderGate_.requestDue") != std::string::npos,
         "callback must route paint cadence through the gate");
  expect(body.find("RenderLock lock(*this)") == std::string::npos,
         "progress callback must never wait behind an e-ink refresh");
}

}  // namespace

int main(int argc, char** argv) {
  if (argc == 2 && std::string(argv[1]) == "--legacy-red") {
    const unsigned paints = legacyPaintCount();
    std::cout << "legacy_paints_10s=" << paints << '\n';
    expect(paints <= 6, "legacy 500 ms progress policy exceeds the repaint budget");
    return EXIT_SUCCESS;
  }

  expect(legacyPaintCount() > 6, "legacy repaint probe must remain capable of detecting the old policy");
  testOneOutstandingPaintDuringSlowRender();
  testPercentAndTerminalUpdatesRemainVisible();
  testCallbackContractAvoidsBlockingOnRender();
  return EXIT_SUCCESS;
}
