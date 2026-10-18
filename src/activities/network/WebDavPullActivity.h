#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <HalStorage.h>

#include "activities/Activity.h"
#include "network/WebDavConfigStore.h"
#include "network/WebDavPull.h"

enum class WebDavPullStatus {
  wifi_selection,
  needs_configuration,
  ready,
  discovering,
  downloading,
  complete,
  failed,
};

enum class WebDavConfigField { Url, Username, Password };

// P2 activity: edits the endpoint, performs a bounded PROPFIND, then reuses
// HttpDownloader for same-origin EPUB downloads.
class WebDavPullActivity final : public Activity {
 public:
  explicit WebDavPullActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("WebDavPull", renderer, mappedInput) {}

  tenorchrome::X3PathIcon navigationIcon() const override { return tenorchrome::X3PathIcon::Network; }
  std::string x3NavigationLabel() const override { return "WebDAV"; }

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return pullStatus == WebDavPullStatus::wifi_selection ||
                                            pullStatus == WebDavPullStatus::discovering ||
                                            pullStatus == WebDavPullStatus::downloading; }

  void editField(WebDavConfigField field);
  void beginPull();
  webdav::QueueResult downloadEntry(const webdav::PropfindEntry& entry, std::string_view destinationPath);
  webdav::QueueResult recordDownload(const webdav::PropfindEntry& entry, bool destinationExists,
                                     bool downloadSucceeded, bool epubValid = true);
  void finishDownloads();

  WebDavPullStatus status() const { return pullStatus; }
  const std::string& statusDetail() const { return detail; }
  const WebDavConfig& config() const { return currentConfig; }
  const webdav::PullQueue& queue() const { return pending; }

 private:
  WebDavConfig currentConfig;
  webdav::PullQueue pending;
  HalFile queueFile;
  std::string queueTempPath;
  std::string queuePath;
  std::size_t queueBytes = 0;
  std::vector<std::string> destinationKeys;
  WebDavPullStatus pullStatus = WebDavPullStatus::needs_configuration;
  std::string detail;
  int selectedField = 0;
  bool runtimeStarted = false;
  bool cancelRequested = false;

  void setFailure(const char* message);
  void startWifiSelection();
  void performPull();
  bool performPropfind();
  bool appendQueueEntry(const webdav::PropfindEntry& entry);
  bool openQueueForRead();
  bool readQueueEntry(webdav::PropfindEntry& entry);
  static bool queueEntrySink(const webdav::PropfindEntry& entry, void* context);
  void handleInput();
  static const char* statusText(WebDavPullStatus value);
};
