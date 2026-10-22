#include "WebDavPullActivity.h"

#include <HalClock.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <NetworkTrust.h>
#include <WiFi.h>
#include <SecureHttpClient.h>

#include <ctime>
#include <algorithm>
#include <cstdint>

#include "CrossPointSettings.h"
#include "FileTransferState.h"
#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/network/NetworkModeSelectionActivity.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/HttpDownloader.h"
#include "util/DictionaryLookupPolicy.h"
#include "shells/Shell.h"

namespace {
std::string originFor(std::string_view url) {
  const size_t schemeEnd = url.find("://");
  if (schemeEnd == std::string_view::npos) return {};
  const size_t pathStart = url.find_first_of("/?#", schemeEnd + 3);
  return std::string(url.substr(0, pathStart == std::string_view::npos ? url.size() : pathStart));
}

bool isEpubPath(std::string_view path) {
  if (path.size() < 5) return false;
  const size_t start = path.size() - 5;
  const auto lower = [](char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
  };
  return path[start] == '.' && lower(path[start + 1]) == 'e' && lower(path[start + 2]) == 'p' &&
         lower(path[start + 3]) == 'u' && lower(path[start + 4]) == 'b';
}

bool validDownloadedEpub(const char* path) {
  char buffer[4096];
  const size_t count = Storage.readFileToBuffer(path, buffer, sizeof(buffer), sizeof(buffer) - 1);
  // shortcut: inspect the first 4 KB, enough for the EPUB mimetype entry; use a streaming ZIP check for arbitrary
  // first-entry offsets if servers with large extra fields need to be supported.
  return count != 0 && webdav::isValidEpub(std::string_view(buffer, count));
}

constexpr std::uint16_t kQueueMagic = 0x4451;

bool writeQueueU16(HalFile& file, const std::uint16_t value) {
  const std::uint8_t bytes[2] = {static_cast<std::uint8_t>(value & 0xffu),
                                 static_cast<std::uint8_t>((value >> 8u) & 0xffu)};
  return file.write(bytes, sizeof(bytes)) == sizeof(bytes);
}

bool writeQueueU64(HalFile& file, const std::uint64_t value) {
  std::uint8_t bytes[8] = {};
  for (std::size_t i = 0; i < sizeof(bytes); ++i) bytes[i] = static_cast<std::uint8_t>(value >> (i * 8u));
  return file.write(bytes, sizeof(bytes)) == sizeof(bytes);
}

bool readQueueU16(HalFile& file, std::uint16_t& value) {
  std::uint8_t bytes[2] = {};
  if (file.read(bytes, sizeof(bytes)) != static_cast<int>(sizeof(bytes))) return false;
  value = static_cast<std::uint16_t>(bytes[0]) | static_cast<std::uint16_t>(bytes[1]) << 8u;
  return true;
}

bool readQueueU64(HalFile& file, std::uint64_t& value) {
  std::uint8_t bytes[8] = {};
  if (file.read(bytes, sizeof(bytes)) != static_cast<int>(sizeof(bytes))) return false;
  value = 0;
  for (std::size_t i = 0; i < sizeof(bytes); ++i) value |= static_cast<std::uint64_t>(bytes[i]) << (i * 8u);
  return true;
}

bool writeQueueString(HalFile& file, const std::string& value) {
  if (value.size() > UINT16_MAX || !writeQueueU16(file, static_cast<std::uint16_t>(value.size()))) return false;
  return value.empty() || file.write(reinterpret_cast<const std::uint8_t*>(value.data()), value.size()) == value.size();
}

bool readQueueString(HalFile& file, std::string& value, const std::size_t limit) {
  std::uint16_t size = 0;
  if (!readQueueU16(file, size) || size > limit) return false;
  value.assign(size, '\0');
  return size == 0 || file.read(value.data(), size) == static_cast<int>(size);
}

std::string remoteUrlFor(const WebDavConfig& config, const webdav::PropfindEntry& entry) {
  const std::string origin = originFor(config.url);
  if (origin.empty()) return {};
  if (entry.href.find("://") != std::string::npos) {
    // The parser already checked absolute hrefs against this origin. Keep the
    // encoded href for the GET, since entry.path is decoded for local storage.
    return webdav::redirectAllowed(config.url, entry.href) ? entry.href : std::string{};
  }
  return entry.href.empty() || entry.href.front() != '/' ? std::string{} : origin + entry.href;
}
}  // namespace

bool WebDavPullActivity::wakePlace(WakePlace& place) const {
  place.kind = WakeKind::Connect;
  place.row = static_cast<int16_t>(NetworkMode::WEB_DAV);
  return true;
}

void WebDavPullActivity::onEnter() {
  runtimeStarted = false;
  cancelRequested = false;
  if (!filetransfer::acquire()) {
    setFailure("WiFi is unavailable");
    Activity::onEnter();
    requestUpdate();
    return;
  }
  runtimeStarted = true;
  Activity::onEnter();
  WEBDAV_STORE.loadFromFile();
  currentConfig = WEBDAV_STORE.getConfig();
  pending = webdav::PullQueue{};
  queuePath = "/.crosspoint/webdav-pull.queue";
  queueTempPath = queuePath + ".tmp";
  queueBytes = 0;
  destinationKeys.clear();
  if (queueFile) queueFile.close();
  Storage.remove(queueTempPath.c_str());
  Storage.remove(queuePath.c_str());
  pullStatus = WebDavConfigStore::isValid(currentConfig) ? WebDavPullStatus::ready
                                                         : WebDavPullStatus::needs_configuration;
  detail.clear();
  selectedField = 0;
  requestUpdate();
}

void WebDavPullActivity::onExit() {
  cancelRequested = true;
  if (queueFile) queueFile.close();
  if (!queueTempPath.empty()) Storage.remove(queueTempPath.c_str());
  if (!queuePath.empty()) Storage.remove(queuePath.c_str());
  Activity::onExit();
  if (runtimeStarted && WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
  filetransfer::release();
  runtimeStarted = false;
}

void WebDavPullActivity::editField(const WebDavConfigField field) {
  const char* title = "WebDAV URL";
  InputType inputType = InputType::Url;
  std::string initial = currentConfig.url.empty() ? "https://" : currentConfig.url;
  if (field == WebDavConfigField::Username) {
    title = "WebDAV username";
    inputType = InputType::Text;
    initial = currentConfig.username;
  } else if (field == WebDavConfigField::Password) {
    title = "WebDAV password";
    inputType = InputType::Password;
    initial = currentConfig.password;
  }

  startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, title, initial, 127, inputType),
                         [this, field](const ActivityResult& result) {
                           if (result.isCancelled) return;
                           const auto* keyboard = std::get_if<KeyboardResult>(&result.data);
                           if (!keyboard) return;
                           WebDavConfig next = currentConfig;
                           if (field == WebDavConfigField::Url) {
                             next.url = keyboard->text;
                             if (next.url == "https://" || next.url == "http://") next = {};
                           }
                           if (field == WebDavConfigField::Username) next.username = keyboard->text;
                           if (field == WebDavConfigField::Password) next.password = keyboard->text;
                           if (!WEBDAV_STORE.setConfig(next)) {
                             setFailure("Invalid WebDAV configuration");
                             requestUpdate();
                             return;
                           }
                           currentConfig = next;
                           pullStatus = WebDavPullStatus::ready;
                           detail.clear();
                           requestUpdate();
                         });
}

void WebDavPullActivity::startWifiSelection() {
  pullStatus = WebDavPullStatus::wifi_selection;
  detail = "Select a WiFi network";
  requestUpdate();
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput), [this](const ActivityResult& result) {
    if (result.isCancelled) {
      pullStatus = WebDavPullStatus::failed;
      detail = "WiFi connection cancelled";
      requestUpdate();
      return;
    }
    const auto* wifi = std::get_if<WifiResult>(&result.data);
    if (!wifi || !wifi->connected) {
      setFailure("WiFi connection failed");
      requestUpdate();
      return;
    }
    performPull();
  });
}

void WebDavPullActivity::beginPull() {
  if (!WebDavConfigStore::isValid(currentConfig)) {
    setFailure("Configure WebDAV first");
    requestUpdate();
    return;
  }
  cancelRequested = false;
  if (WiFi.status() == WL_CONNECTED) {
    performPull();
  } else {
    startWifiSelection();
  }
}

bool WebDavPullActivity::performPropfind() {
  freeink::http_url::Parts parsed;
  if (!freeink::http_url::parse(currentConfig.url, parsed)) return false;
  if (parsed.tls && time(nullptr) < 1735689600 && !halClock.syncFromNTP()) return false;

  if (!Storage.ensureDirectoryExists("/.crosspoint") ||
      !Storage.openFileForWrite("WEBDAV", queueTempPath.c_str(), queueFile))
    return false;
  queueBytes = 0;

  freeink::SecureHttpClient http;
  http.setCACert(network_trust::forUrl(currentConfig.url));
#if defined(SIMULATOR_DEVICE_X3) || defined(SIMULATOR_DEVICE_X4_PRO) || defined(SIMULATOR_DEVICE_X4_CLASSIC)
  // The native simulator adapter exposes the legacy buffered HTTP surface.
  // Keep the production path below streaming, and feed the same bounded parser
  // from the adapter body so host builds exercise parser and queue behavior.
  http.addHeader("User-Agent", "CrossPoint-ESP32-WebDAV");
#else
  http.setUserAgent("CrossPoint-ESP32-WebDAV");
  http.setBasicAuth(currentConfig.username, currentConfig.password);
  http.setFollowRedirects(0);  // PROPFIND redirects require a new same-origin request by the caller.
#endif
  if (!http.begin(currentConfig.url)) return false;
  http.addHeader("Depth", "1");
  http.addHeader("Content-Type", "application/xml; charset=utf-8");
  const std::string request = "<?xml version=\"1.0\" encoding=\"utf-8\"?><D:propfind xmlns:D=\"DAV:\"><D:prop>"
                              "<D:resourcetype/><D:getcontentlength/><D:getcontenttype/></D:prop></D:propfind>";
  webdav::PropfindStreamParser parser(currentConfig.url);
#if defined(SIMULATOR_DEVICE_X3) || defined(SIMULATOR_DEVICE_X4_PRO) || defined(SIMULATOR_DEVICE_X4_CLASSIC)
  const int code = http.sendRequest("PROPFIND", request);
  const String response = http.getString();
  bool streamed = true;
  for (std::size_t offset = 0; streamed && offset < response.length();) {
    const std::size_t count = std::min<std::size_t>(1024u, response.length() - offset);
    streamed = parser.feed(reinterpret_cast<const std::uint8_t*>(response.c_str() + offset), count,
                           &WebDavPullActivity::queueEntrySink, this);
    offset += count;
  }
  const bool complete = streamed && !cancelRequested;
#else
  const int code = http.sendRequest(
      "PROPFIND", reinterpret_cast<const uint8_t*>(request.data()), request.size(),
      [&parser, this](const uint8_t* data, const size_t size) {
        return parser.feed(data, size, &WebDavPullActivity::queueEntrySink, this);
      },
      [this] {
        // The loop is blocked for the whole request: pump input here so Back can cancel it.
        mappedInput.update(true);
        if (mappedInput.wasReleased(MappedInputManager::Button::Back)) cancelRequested = true;
        return cancelRequested;
      });
  const bool complete = http.responseComplete() && !http.callbackAborted() && !http.aborted();
#endif
  http.end();
  const bool parsedOk = complete && code == 207 && parser.finish();
  if (queueFile) {
    const bool synced = queueFile.sync();
    queueFile.close();
    if (!synced) {
      Storage.remove(queueTempPath.c_str());
      return false;
    }
  }
  if (cancelRequested || !parsedOk) {
    Storage.remove(queueTempPath.c_str());
    return false;
  }
  Storage.remove(queuePath.c_str());
  if (!Storage.rename(queueTempPath.c_str(), queuePath.c_str())) return false;
  return true;
}

void WebDavPullActivity::performPull() {
  pullStatus = WebDavPullStatus::discovering;
  detail.clear();
  requestUpdateAndWait();

  if (!performPropfind()) {
    setFailure(cancelRequested ? "WebDAV pull cancelled" : "WebDAV request failed");
    requestUpdate();
    return;
  }
  if (!openQueueForRead()) {
    setFailure("WebDAV queue failed");
    requestUpdate();
    return;
  }

  std::string folder = SETTINGS.opdsDownloadFolder;
  if (folder.empty()) folder = "/";
  if (folder.front() != '/') folder.insert(folder.begin(), '/');
  while (folder.size() > 1 && folder.back() == '/') folder.pop_back();
  if (!Storage.ensureDirectoryExists(folder.c_str())) {
    setFailure("Cannot open download folder");
    requestUpdate();
    return;
  }

  destinationKeys.clear();
  webdav::PropfindEntry entry;
  while (readQueueEntry(entry)) {
    if (cancelRequested) break;
    if (entry.collection || !isEpubPath(entry.path)) continue;
    const size_t slash = entry.path.rfind('/');
    const std::string name = slash == std::string::npos ? entry.path : entry.path.substr(slash + 1);
    const std::string destination = folder == "/" ? "/" + name : folder + "/" + name;
    const std::string destinationKey = DictionaryLookupPolicy::comparatorKey(destination);
    if (std::find(destinationKeys.begin(), destinationKeys.end(), destinationKey) != destinationKeys.end()) {
      detail = "WebDAV filename collision";
      continue;
    }
    destinationKeys.push_back(destinationKey);
    const auto result = downloadEntry(entry, destination);
    if (!webdav::pullContinuesAfter(result.status)) break;
  }
  if (cancelRequested) {
    setFailure("WebDAV pull cancelled");
  } else if (pullStatus != WebDavPullStatus::failed) {
    finishDownloads();
  }
  if (queueFile) queueFile.close();
  Storage.remove(queuePath.c_str());
  requestUpdate();
}

bool WebDavPullActivity::queueEntrySink(const webdav::PropfindEntry& entry, void* const context) {
  return context && static_cast<WebDavPullActivity*>(context)->appendQueueEntry(entry);
}

bool WebDavPullActivity::appendQueueEntry(const webdav::PropfindEntry& entry) {
  if (entry.href.size() > webdav::WebDavPullLimits::maxHref || entry.path.size() > webdav::WebDavPullLimits::maxPath ||
      entry.contentType.size() > webdav::WebDavPullLimits::maxProperty || entry.depth > webdav::WebDavPullLimits::maxDepth)
    return false;
  const std::size_t cost = 2u + 2u + 2u + 2u + 8u + 1u + 1u + entry.href.size() + entry.path.size() + entry.contentType.size();
  if (cost > webdav::WebDavPullLimits::maxEntry || queueBytes > webdav::WebDavPullLimits::maxQueueBytes - cost)
    return false;
  if (!writeQueueU16(queueFile, kQueueMagic) || !writeQueueString(queueFile, entry.href) ||
      !writeQueueString(queueFile, entry.path) || !writeQueueString(queueFile, entry.contentType) ||
      !writeQueueU64(queueFile, entry.contentLength))
    return false;
  const std::uint8_t flags[2] = {static_cast<std::uint8_t>(entry.depth), static_cast<std::uint8_t>(entry.collection)};
  if (queueFile.write(flags, sizeof(flags)) != sizeof(flags)) return false;
  queueBytes += cost;
  return true;
}

bool WebDavPullActivity::openQueueForRead() {
  if (queueFile) queueFile.close();
  return Storage.openFileForRead("WEBDAV", queuePath.c_str(), queueFile);
}

bool WebDavPullActivity::readQueueEntry(webdav::PropfindEntry& entry) {
  std::uint16_t magic = 0;
  if (!readQueueU16(queueFile, magic)) return false;
  if (magic != kQueueMagic || !readQueueString(queueFile, entry.href, webdav::WebDavPullLimits::maxHref) ||
      !readQueueString(queueFile, entry.path, webdav::WebDavPullLimits::maxPath) ||
      !readQueueString(queueFile, entry.contentType, webdav::WebDavPullLimits::maxProperty) ||
      !readQueueU64(queueFile, entry.contentLength)) {
    setFailure("WebDAV queue is corrupt");
    return false;
  }
  std::uint8_t flags[2] = {};
  if (queueFile.read(flags, sizeof(flags)) != static_cast<int>(sizeof(flags)) || flags[0] > webdav::WebDavPullLimits::maxDepth) {
    setFailure("WebDAV queue is corrupt");
    return false;
  }
  entry.depth = flags[0];
  entry.collection = flags[1] != 0;
  return true;
}

webdav::QueueResult WebDavPullActivity::downloadEntry(const webdav::PropfindEntry& entry,
                                                       const std::string_view destinationPath) {
  if (!WebDavConfigStore::isValid(currentConfig) || !webdav::validLocalPath(entry.path) || !webdav::validLocalPath(destinationPath)) {
    setFailure("Invalid WebDAV path");
    return {webdav::PullStatus::invalid, false};
  }
  if (Storage.exists(std::string(destinationPath).c_str()))
    return recordDownload(entry, true, true);

  const std::string remoteUrl = remoteUrlFor(currentConfig, entry);
  if (remoteUrl.empty()) {
    setFailure("Invalid WebDAV URL");
    return {webdav::PullStatus::invalid, false};
  }
  const auto progress = [this](size_t, size_t) {
    // The loop is blocked for the whole download: pump input here so Back can cancel it.
    mappedInput.update(true);
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) cancelRequested = true;
  };
  // The EPUB check runs on the staged copy: once committed, a failed re-read would delete the book.
  const auto transfer = HttpDownloader::downloadToFile(remoteUrl, std::string(destinationPath), progress, &cancelRequested,
                                                        currentConfig.username, currentConfig.password, false,
                                                        validDownloadedEpub);
  // CACHE_ERROR still committed the book; INVALID_EPUB arrived whole and was refused.
  const bool committed = transfer == HttpDownloader::OK || transfer == HttpDownloader::CACHE_ERROR;
  return recordDownload(entry, false, committed || transfer == HttpDownloader::INVALID_EPUB, committed);
}

webdav::QueueResult WebDavPullActivity::recordDownload(const webdav::PropfindEntry& entry,
                                                       const bool destinationExists, const bool downloadSucceeded,
                                                       const bool epubValid) {
  pullStatus = WebDavPullStatus::downloading;
  const webdav::PullStatus status = webdav::downloadStatus(entry, destinationExists, downloadSucceeded, epubValid);
  const webdav::QueueResult result{status, status == webdav::PullStatus::downloaded};
  if (result.status == webdav::PullStatus::failed) setFailure("WebDAV download failed");
  if (result.status == webdav::PullStatus::invalid) setFailure("Invalid EPUB");
  return result;
}

void WebDavPullActivity::finishDownloads() { pullStatus = webdav::statusAfterPull(pullStatus); }

void WebDavPullActivity::setFailure(const char* message) {
  pullStatus = WebDavPullStatus::failed;
  detail = message ? message : "WebDAV failed";
}

const char* WebDavPullActivity::statusText(const WebDavPullStatus value) {
  switch (value) {
    case WebDavPullStatus::wifi_selection: return "Connecting WiFi";
    case WebDavPullStatus::needs_configuration: return "Configure WebDAV";
    case WebDavPullStatus::ready: return "Ready";
    case WebDavPullStatus::discovering: return "Reading WebDAV listing";
    case WebDavPullStatus::downloading: return "Downloading books";
    case WebDavPullStatus::complete: return "WebDAV pull complete";
    case WebDavPullStatus::failed: return "WebDAV pull failed";
  }
  return "WebDAV";
}

void WebDavPullActivity::render(RenderLock&&) {
  renderer.clearScreen();
  drawNavigationHeader("WebDAV");
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int left = 18;
  int y = 76;
  renderer.drawText(UI_10_FONT_ID, left, y, statusText(pullStatus));
  y += lineHeight + 12;
  const char* labels[] = {"URL", "Username", "Password", "Pull books"};
  const char* values[] = {currentConfig.url.empty() ? "Not set" : currentConfig.url.c_str(),
                          currentConfig.username.empty() ? "Not set" : currentConfig.username.c_str(),
                          currentConfig.password.empty() ? "Not set" : "******", ""};
  const bool editing = pullStatus == WebDavPullStatus::ready || pullStatus == WebDavPullStatus::needs_configuration ||
                       pullStatus == WebDavPullStatus::failed;
  for (int row = 0; row < 4; ++row) {
    const bool selected = editing && row == selectedField;
    std::string line = selected ? "> " : "  ";
    line += labels[row];
    if (row < 3) line += ": " + std::string(values[row]);
    const std::string visible = renderer.truncatedText(UI_10_FONT_ID, line.c_str(), renderer.getScreenWidth() - left - 12);
    renderer.drawText(UI_10_FONT_ID, left, y, visible.c_str(), true, selected ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    y += lineHeight + 8;
  }
  if (!detail.empty()) renderer.drawText(UI_10_FONT_ID, left, y + 6, detail.c_str());
  const auto buttonLabels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, buttonLabels.btn1, buttonLabels.btn2, buttonLabels.btn3, buttonLabels.btn4);
  renderer.displayBuffer();
}

void WebDavPullActivity::handleInput() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (pullStatus == WebDavPullStatus::discovering || pullStatus == WebDavPullStatus::downloading ||
        pullStatus == WebDavPullStatus::wifi_selection) {
      cancelRequested = true;
      return;
    }
    finish();
    return;
  }
  if (pullStatus != WebDavPullStatus::ready && pullStatus != WebDavPullStatus::needs_configuration &&
      pullStatus != WebDavPullStatus::failed)
    return;
  if (mappedInput.wasReleased(MappedInputManager::Button::NavPrevious)) {
    selectedField = (selectedField + 3) % 4;
    requestUpdate();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::NavNext)) {
    selectedField = (selectedField + 1) % 4;
    requestUpdate();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (selectedField < 3)
      editField(static_cast<WebDavConfigField>(selectedField));
    else
      beginPull();
  }
}

void WebDavPullActivity::loop() {
  handleInput();
}
