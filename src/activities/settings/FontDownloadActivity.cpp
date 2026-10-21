#include "FontDownloadActivity.h"
#include "activities/settings/X3SettingsLayout.h"

#include <ArduinoJson.h>
#include <FontCacheManager.h>
#include <HeapFloor.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <HalPowerManager.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>
#include <esp_rom_crc.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "MappedInputManager.h"
#include "FileTransferState.h"
#include "SdCardFontSystem.h"
#include "FontPackInstaller.h"
#include "FontStoreCatalog.h"
#include "ReaderInkWeight.h"
#include "CrossPointSettings.h"
#include "SettingsList.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/settings/FontDownloadTransfer.h"
#include "activities/settings/FontDownloadHeap.h"
#include "components/UITheme.h"
#include "components/SettledListRender.h"
#include "fontIds.h"
#include "network/HttpDownloader.h"
#include "network/HttpRangeTransfer.h"
#include "network/WebDavReplace.h"
#include "shells/Shell.h"
#include "shells/ugly/UglyNote.h"

namespace fui = freeink::ui;

#ifdef TENOR_PRESS_PROBE
namespace {
void logFontDownloadHeap(const char* stage) {
  LOG_INF("FONT_HEAP", "stage=%s free=%u largest=%u min=%u", stage, ESP.getFreeHeap(),
          ESP.getMaxAllocHeap(), ESP.getMinFreeHeap());
}
}
#endif

FontDownloadActivity::FontDownloadActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const bool toBook)
    : UiListActivity("FontDownload", renderer, mappedInput), fontInstaller_(sdFontSystem.registry()), toBook_(toBook) {}

bool FontDownloadActivity::installDownloadedPack(const char* path, uint32_t size, uint32_t crc) {
  const auto workspace = FontPackInstaller::workingSetBytes();
  LOG_INF("FONT", "Pack install start free=%u largest=%u min=%u workspace=%u", ESP.getFreeHeap(),
          ESP.getMaxAllocHeap(), ESP.getMinFreeHeap(), static_cast<unsigned>(workspace));
  if (!fontdownload::canInstallPack(ESP.getFreeHeap(), ESP.getMaxAllocHeap(), workspace)) {
    failureReason_ = tr(STR_MEMORY_ERROR);
    errorMessage_ = failureReason_;
    return false;
  }
  const auto result = FontPackInstaller::install(path, size, crc);
  if (result != FontPackInstaller::Result::OK) {
    LOG_ERR("FONT", "Pack install failed path=%s result=%d", path, static_cast<int>(result));
    failureReason_ = result == FontPackInstaller::Result::INVALID_PACK ? tr(STR_FONT_PACK_INVALID)
                                                                     : tr(STR_FONT_PACK_INSTALL_ERROR);
    errorMessage_ = failureReason_;
    return false;
  }
  sdFontSystem.markRegistryDirty();
  LOG_INF("FONT", "Pack committed free=%u largest=%u min=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap(),
          ESP.getMinFreeHeap());
  return true;
}

void FontDownloadActivity::activateIndex(const int index) {
  switch (state_) {
    case STORE_LIST:
      nav.selected = index;
      app.clearTapFlash();
      activateStoreRow(index);
      return;
    case GROUP_LIST:
      app.clearTapFlash();
      enterGroup(index);
      requestUpdate();
      return;
    case FAMILY_LIST:
      nav.selected = index;
      // Activation starts a download or opens the delete prompt; a lingering
      // flash would gray an unrelated row.
      app.clearTapFlash();
      activateSelected();  // ends with requestUpdateAndWait itself
      return;
    case WIFI_SELECTION:
    case LOADING_MANIFEST:
    case DOWNLOADING:
    case COMPLETE:
    case ERROR:
      return;
  }
}

fui::ListNav& FontDownloadActivity::activeNav() { return state_ == GROUP_LIST ? groupNav_ : nav; }

void FontDownloadActivity::onBackButton() {
  if (state_ != FAMILY_LIST || !hasGroupScreen()) {
    finish();
    return;
  }

  closeRouting();
  {
    RenderLock lock(*this);
    state_ = GROUP_LIST;
    rowsDirty_ = true;
  }
  requestUpdate();
}

// --- Lifecycle ---

void FontDownloadActivity::onEnter() {
  runtimeStarted_ = false;
  terminalIdleTimerStarted_ = false;
  // Keep BLE stopped for Wi-Fi selection, TLS downloads, and SD replacement.
  if (!filetransfer::acquire()) {
    LOG_ERR("FONT", "BLE teardown incomplete; leaving font download");
    finish();
    return;
  }
  runtimeStarted_ = true;
  UiListActivity::onEnter();
  // The store opens on the fonts already on the card: no network, no TLS heap, nothing to wait for.
  loadInstalledList();
  RenderLock lock(*this);
  nav.reset();
  state_ = STORE_LIST;
  rowsDirty_ = true;
}

// The fonts on the card, read from the registry; "Update" from the receipt of a pack against this firmware's store.
void FontDownloadActivity::loadInstalledList() {
  sdFontSystem.refreshIfDirty();
  std::vector<InstalledFont> list;
  const auto& families = sdFontSystem.registry().getFamilies();
  list.reserve(families.size());
  for (const auto& family : families) {
    InstalledFont font;
    font.name = family.name;
    const char* root = SdCardFontRegistry::findFamilyRoot(font.name.c_str());
    if (root && FontPackInstaller::isPackFamily(root, font.name.c_str())) {
      for (size_t i = 0; i < fontstore::PACK_COUNT; ++i) {
        if (font.name != fontstore::PACKS[i].name) continue;
        font.hasUpdate = !FontPackInstaller::matchesInstalled(root, font.name.c_str(), fontstore::PACKS[i].size,
                                                              fontstore::PACKS[i].crc32);
        break;
      }
    }
    list.push_back(std::move(font));
  }
  std::sort(list.begin(), list.end(), [](const InstalledFont& a, const InstalledFont& b) { return a.name < b.name; });
  RenderLock lock(*this);
  installed_ = std::move(list);
  rowsDirty_ = true;
}

// Joins Wi-Fi and loads the store's list (to add fonts, or to fetch the one font named in updateTarget_).
void FontDownloadActivity::beginNetwork() {
  // This screen reads installation state directly from SD. Drop the rebuildable
  // registry before TLS/JSON allocations fragment the heap; keep loaded fonts
  // and CJK UI fallback pointers alive. Leaving Wi-Fi already restarts the app.
  {
    RenderLock lock(*this);
    const uint32_t before = ESP.getFreeHeap();
    sdFontSystem.registry() = SdCardFontRegistry{};
    sdFontSystem.markRegistryDirty();
    installed_.clear();
    installed_.shrink_to_fit();
    LOG_INF("FONT", "Download registry released=%u heap=%u largest=%u", before <= ESP.getFreeHeap() ? ESP.getFreeHeap() - before : 0u,
            ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    state_ = WIFI_SELECTION;
  }
  WiFi.mode(WIFI_STA);
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void FontDownloadActivity::onExit() {
  Activity::onExit();

  if (runtimeStarted_) {
    if (networkUsed_ || WiFi.getMode() != WIFI_MODE_NULL) {
      WiFi.disconnect(false);
      delay(30);
      toBook_ ? silentRestartToReader() : silentRestartToFileTransfer();
    }
  }

  filetransfer::release();
}

void FontDownloadActivity::onWifiSelectionComplete(const bool success) {
  if (!success) {
    // Backed out of Wi-Fi: no TLS ran, so the store list comes back as it was, with the radio off.
    WiFi.mode(WIFI_OFF);
    updateTarget_.clear();
    loadInstalledList();
    {
      RenderLock lock(*this);
      nav.reset();
      state_ = STORE_LIST;
      rowsDirty_ = true;
    }
    requestUpdate();
    return;
  }
  networkUsed_ = true;

  {
    RenderLock lock(*this);
#ifdef TENOR_PRESS_PROBE
    logFontDownloadHeap("after-wifi");
#endif
    // Wi-Fi time/timezone persistence may have rebuilt the settings catalog.
    // Release it after that child exits and before manifest/file TLS starts.
#ifdef ESP_PLATFORM
    const uint32_t before = ESP.getFreeHeap();
#endif
    releaseBaseSettingsList();
#ifdef TENOR_PRESS_PROBE
    logFontDownloadHeap("settings-released");
#endif
    if (auto* cache = renderer.getFontCacheManager()) cache->releaseSdFontCaches();
#ifdef TENOR_PRESS_PROBE
    logFontDownloadHeap("wifi-caches-released");
#endif
    sdFontSystem.releaseReaderForDownload(renderer);
#ifdef TENOR_PRESS_PROBE
    logFontDownloadHeap("wifi-reader-released");
#endif
#ifdef ESP_PLATFORM
    const uint32_t after = ESP.getFreeHeap();
    LOG_INF("FONT", "Settings catalog released=%u heap=%u largest=%u", after >= before ? after - before : 0u,
            after, ESP.getMaxAllocHeap());
#endif
    state_ = LOADING_MANIFEST;
  }
  requestUpdateAndWait();

  if (!fetchAndParseManifest()) {
    // Drop whatever was parsed before the failure: it would otherwise sit in
    // the heap behind the error screen, and leave the retry path pointing at a
    // half-built family table.
    clearManifest();
    {
      RenderLock lock(*this);
      state_ = ERROR;
    }
    return;
  }
#ifdef TENOR_PRESS_PROBE
  logFontDownloadHeap("after-manifest");
  LOG_INF("FONT_HEAP", "catalog arena=%u files=%u families=%u groups=%u indices=%u rows=%u/%u", arenaCapacity_,
          fileEntryCount_ * static_cast<unsigned>(sizeof(ManifestFile)),
          static_cast<unsigned>(families_.capacity() * sizeof(ManifestFamily)),
          static_cast<unsigned>(scriptGroupLabels_.capacity() * sizeof(StrRef)),
          static_cast<unsigned>(filteredIndices_.capacity() * sizeof(int)),
          static_cast<unsigned>(rowLabels_.capacity() * sizeof(std::string)),
          static_cast<unsigned>(rowItems_.capacity() * sizeof(fui::ListItem)));
#endif

#ifdef FREEINK_TLS_AUDIT
  if (auditDownload_) {
    ManifestFamily* selected = nullptr;
    for (auto& family : families_) {
      if (!family.installed && family.fileCount && (!selected || family.totalSize < selected->totalSize))
        selected = &family;
    }
    if (selected) {
      LOG_INF("TLS_AUDIT", "Production font download family=%s files=%u bytes=%u", str(selected->name),
              selected->fileCount, selected->totalSize);
      currentFileIndex_ = 0;
      currentFileTotal_ = selected->fileCount;
      downloadFamily(*selected);
      LOG_INF("TLS_AUDIT", "Production font result complete=%d files=%u/%u free=%u largest=%u min=%u",
              state_ == COMPLETE, (unsigned)currentFileIndex_, (unsigned)currentFileTotal_, ESP.getFreeHeap(),
              ESP.getMaxAllocHeap(), ESP.getMinFreeHeap());
      requestUpdate();
      return;
    }
    LOG_INF("TLS_AUDIT", "No uninstalled font family available");
  }
#endif

  if (!updateTarget_.empty()) {
    int target = -1;
    for (size_t i = 0; i < families_.size(); ++i) {
      if (updateTarget_ == str(families_[i].name)) target = static_cast<int>(i);
    }
    if (target < 0 || !families_[target].hasUpdate) {
      RenderLock lock(*this);
      downloadingFamilyIndex_ = -1;
      errorMessage_ = tr(STR_FONT_NO_UPDATE);
      state_ = ERROR;
      return;
    }
    currentFileIndex_ = 0;
    currentFileTotal_ = families_[target].fileCount;
    downloadFamily(families_[target]);
    requestUpdate();
    return;
  }

  if (!hasGroupScreen()) buildFilteredIndices(0);

#ifdef SIMULATOR
  if (const char* spec = std::getenv("CROSSPOINT_SIM_FONT_STATE")) {
    simulateState(spec);
    requestUpdate();
    return;
  }
#endif
  {
    RenderLock lock(*this);
    rowsDirty_ = true;  // families_ just loaded
    if (hasGroupScreen()) {
      groupNav_.reset();
      state_ = GROUP_LIST;
    } else {
      nav.reset();
      state_ = FAMILY_LIST;
    }
  }
}

#ifdef SIMULATOR
// Test hook: CROSSPOINT_SIM_FONT_STATE="state=downloading,fam=5,fp=..,fd=..,b=1,bi=1,bc=2,bt=..,bd=.." freezes a
// transfer screen on chosen numbers so a screenshot can read them. state: downloading, installing, done, error.
void FontDownloadActivity::simulateState(const char* spec) {
  RenderLock lock(*this);
  const std::string text = spec;
  size_t from = 0;
  std::string mode;
  while (from < text.size()) {
    size_t to = text.find(',', from);
    if (to == std::string::npos) to = text.size();
    const std::string item = text.substr(from, to - from);
    const size_t eq = item.find('=');
    const std::string key = item.substr(0, eq);
    const std::string value = eq == std::string::npos ? "" : item.substr(eq + 1);
    const auto number = std::strtoull(value.c_str(), nullptr, 10);
    if (key == "state") mode = value;
    else if (key == "fam") downloadingFamilyIndex_ = static_cast<int>(number);
    else if (key == "fp") fileProgress_ = static_cast<size_t>(number);
    else if (key == "ft") fileTotal_ = static_cast<size_t>(number);
    else if (key == "fd") familyDownloadedBytes_ = static_cast<uint32_t>(number);
    else if (key == "b") batchRunning_ = number != 0;
    else if (key == "bi") batchFamilyIndex_ = static_cast<unsigned>(number);
    else if (key == "bc") batchFamilyCount_ = static_cast<unsigned>(number);
    else if (key == "bt") batchTotalBytes_ = number;
    else if (key == "bd") batchDownloadedBytes_ = number;
    else if (key == "ok") batchSuccessCount_ = static_cast<unsigned>(number);
    else if (key == "bad") batchFailureCount_ = static_cast<unsigned>(number);
    else if (key == "failfam") {
      resultFailureIndex_ = static_cast<int>(number);
      families_[resultFailureIndex_].failureReason = tr(STR_FONT_PACK_CHECKSUM_ERROR);
    } else if (key == "msg") errorMessage_ = value;
    from = to + 1;
  }
  batchResult_ = mode == "done" && batchFamilyCount_ > 0;
  installingPack_ = mode == "installing";
  state_ = mode == "done" ? COMPLETE : mode == "error" ? ERROR : DOWNLOADING;
  if (state_ == DOWNLOADING) {
    currentFileIndex_ = 0;
    currentFileTotal_ = 1;
  }
}
#endif

// --- Manifest fetching ---

void FontDownloadActivity::clearManifest() {
  // Swap rather than clear: clear() keeps the capacity, and this runs to hand
  // the heap back while the error screen is up. Reverse allocation order.
  std::vector<int>().swap(filteredIndices_);
  std::vector<ManifestFamily>().swap(families_);
  std::vector<StrRef>().swap(scriptGroupLabels_);
  files_.reset();
  fileEntryCount_ = 0;
  stringArena_.reset();
  arenaUsed_ = 0;
  arenaCapacity_ = 0;
}

bool FontDownloadActivity::internString(const char* text, StrRef& outRef) {
  if (text == nullptr || *text == '\0') {
    outRef = 0;
    return true;
  }
  const size_t length = std::strlen(text) + 1;
  if (arenaUsed_ + length > arenaCapacity_) {
    LOG_ERR("FONT", "Manifest string arena overflow at %u/%u bytes", arenaUsed_, arenaCapacity_);
    return false;
  }
  outRef = arenaUsed_;
  std::memcpy(stringArena_.get() + arenaUsed_, text, length);
  arenaUsed_ = static_cast<uint32_t>(arenaUsed_ + length);
  return true;
}

bool FontDownloadActivity::fetchAndParseManifest() {
  // Download manifest to a temp file on SD card to avoid holding both
  // TLS buffers and the full JSON string in RAM simultaneously.
  static constexpr const char* MANIFEST_TMP = "/fonts_manifest.tmp";

  if (auto* fcm = renderer.getFontCacheManager()) {
    RenderLock lock(*this);
    fcm->releaseSdFontCaches();
  }
  if (ESP.getFreeHeap() < HttpDownloader::MIN_TLS_FREE_HEAP ||
      ESP.getMaxAllocHeap() < HttpDownloader::MIN_TLS_MAX_ALLOC) {
    LOG_ERR("FONT", "Low heap for manifest (%u free, %u max block)", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    errorMessage_ = tr(STR_MEMORY_ERROR);
    return false;
  }

  // No downgradeRedirectsToHttp here, unlike the font transfers below: this
  // response carries the crc32 values that are the only integrity anchor for
  // those plain-HTTP downloads.
  auto result = HttpDownloader::downloadToFile(FONT_MANIFEST_URL, MANIFEST_TMP, nullptr);
#ifdef TENOR_PRESS_PROBE
  logFontDownloadHeap("manifest-http-closed");
#endif
  if (result != HttpDownloader::OK) {
    LOG_ERR("FONT", "Failed to fetch manifest from %s", FONT_MANIFEST_URL);
    errorMessage_ = tr(STR_FONT_LIST_FETCH_FAILED);
    Storage.remove(MANIFEST_TMP);
    return false;
  }

  // HTTP client is now closed - TLS buffers freed. Parse JSON from file.
  HalFile manifestFile;
  if (!Storage.openFileForRead("FONT", MANIFEST_TMP, manifestFile)) {
    LOG_ERR("FONT", "Failed to open temp manifest");
    Storage.remove(MANIFEST_TMP);
    errorMessage_ = tr(STR_FONT_LIST_READ_FAILED);
    return false;
  }

  JsonDocument doc;
  DeserializationError err;
  {
    // "styles" is the only key the catalog never reads. Dropping it keeps the
    // DOM about 1KB smaller while it coexists with the arena allocated below.
    JsonDocument filter;
    filter["version"] = true;
    filter["baseUrl"] = true;
    filter["scriptGroups"][0]["tag"] = true;
    filter["scriptGroups"][0]["label"] = true;
    filter["families"][0]["name"] = true;
    filter["families"][0]["description"] = true;
    filter["families"][0]["scripts"] = true;
    filter["families"][0]["files"][0]["name"] = true;
    filter["families"][0]["files"][0]["size"] = true;
    filter["families"][0]["files"][0]["crc32"] = true;
    err = deserializeJson(doc, manifestFile, DeserializationOption::Filter(filter));
  }
  manifestFile.close();
  Storage.remove(MANIFEST_TMP);

  if (err) {
    LOG_ERR("FONT", "Manifest parse error: %s", err.c_str());
    errorMessage_ = tr(STR_INVALID_FONT_MANIFEST);
    return false;
  }

  int version = doc["version"] | 0;
  if (version != FONTS_MANIFEST_VERSION) {
    LOG_ERR("FONT", "Unsupported manifest version: %d", version);
    errorMessage_ = tr(STR_FONT_MANIFEST_VERSION_UNSUPPORTED);
    return false;
  }

  // Validate every schema field that can affect allocation or transfer before
  // clearing the current catalog or consulting SD state. In particular, an
  // absent/empty family file list used to reach ensureFamilyDir() as a
  // successful zero-file download.
  if (!font_manifest::validateRequiredShape(doc)) {
    LOG_ERR("FONT", "Malformed manifest families/files/size fields");
    errorMessage_ = tr(STR_INVALID_FONT_MANIFEST);
    return false;
  }

  const char* manifestBaseUrl = doc["baseUrl"].as<const char*>();
  if (!font_manifest::isSupportedBaseUrl(manifestBaseUrl)) {
    LOG_ERR("FONT", "Malformed manifest baseUrl");
    errorMessage_ = tr(STR_INVALID_FONT_MANIFEST);
    return false;
  }

  baseUrl_ = manifestBaseUrl;
  downloadUrl_.reserve(baseUrl_.size() + 128);
  clearManifest();
  sdFontSystem.markRegistryDirty();

  JsonArray groupsArr = doc["scriptGroups"].as<JsonArray>();
  JsonArray familiesArr = doc["families"].as<JsonArray>();

  // Size the arena and the file table in one pass so neither reallocates while
  // the catalog is built: a mid-build growth would both fragment the heap and
  // invalidate arena pointers already handed out below.
  const size_t groupCount = std::min(groupsArr.size(), MAX_SCRIPT_GROUPS);
  size_t arenaBytes = 1;  // leading terminator makes offset 0 the empty string
  size_t manifestFileCount = 0;
  const auto addArenaString = [&arenaBytes](const char* text) {
    const size_t length = std::strlen(text);
    if (length == std::numeric_limits<size_t>::max()) return false;
    const size_t bytes = length + 1;
    if (arenaBytes > std::numeric_limits<size_t>::max() - bytes) return false;
    arenaBytes += bytes;
    return arenaBytes <= std::numeric_limits<uint32_t>::max();
  };
  for (size_t groupIndex = 0; groupIndex < groupCount; groupIndex++) {
    if (!addArenaString(groupsArr[groupIndex]["label"] | "")) {
      LOG_ERR("FONT", "Manifest string arena size overflow");
      errorMessage_ = tr(STR_INVALID_FONT_MANIFEST);
      return false;
    }
  }
  for (JsonObject fObj : familiesArr) {
    if (!addArenaString(fObj["name"] | "") || !addArenaString(fObj["description"] | "")) {
      LOG_ERR("FONT", "Manifest string arena size overflow");
      errorMessage_ = tr(STR_INVALID_FONT_MANIFEST);
      return false;
    }
    for (JsonObject fileObj : fObj["files"].as<JsonArray>()) {
      if (!addArenaString(fileObj["name"] | "")) {
        LOG_ERR("FONT", "Manifest string arena size overflow");
        errorMessage_ = tr(STR_INVALID_FONT_MANIFEST);
        return false;
      }
      if (manifestFileCount >= std::numeric_limits<uint32_t>::max()) {
        LOG_ERR("FONT", "Manifest file entry count overflow");
        errorMessage_ = tr(STR_INVALID_FONT_MANIFEST);
        return false;
      }
      manifestFileCount++;
    }
  }
  const uint64_t catalogBytes = uint64_t{arenaBytes} + uint64_t{manifestFileCount} * sizeof(ManifestFile) +
                                uint64_t{groupCount} * sizeof(StrRef) +
                                uint64_t{familiesArr.size()} * (sizeof(ManifestFamily) + sizeof(int)) + 128;
  if (!fontdownload::canAllocateCatalog(catalogBytes, ESP.getFreeHeap(), ESP.getMaxAllocHeap())) {
    errorMessage_ = tr(STR_MEMORY_ERROR);
    return false;
  }
  stringArena_ = heapfloor::array<char[]>(arenaBytes, "font-catalog");
  if (!stringArena_) {
    LOG_ERR("FONT", "OOM: %zu byte string arena", arenaBytes);
    errorMessage_ = tr(STR_MEMORY_ERROR);
    return false;
  }
  stringArena_[0] = '\0';
  arenaUsed_ = 1;
  arenaCapacity_ = static_cast<uint32_t>(arenaBytes);
  files_ = makeUniqueNoThrow<ManifestFile[]>(manifestFileCount);
  if (!files_) {
    LOG_ERR("FONT", "OOM: %zu manifest file entries", manifestFileCount);
    errorMessage_ = tr(STR_MEMORY_ERROR);
    return false;
  }

  scriptGroupLabels_.reserve(groupCount);
  if (groupsArr.size() > MAX_SCRIPT_GROUPS) {
    LOG_ERR("FONT", "Manifest declares more than %zu script groups; extra groups ignored", MAX_SCRIPT_GROUPS);
  }
  for (size_t groupIndex = 0; groupIndex < groupCount; groupIndex++) {
    JsonObject groupObj = groupsArr[groupIndex].as<JsonObject>();
    const char* tag = groupObj["tag"] | "";
    const char* label = groupObj["label"] | "";
    if (*tag == '\0' || *label == '\0') {
      LOG_ERR("FONT", "Malformed script group at index %zu", groupIndex);
      errorMessage_ = tr(STR_INVALID_FONT_MANIFEST);
      return false;
    }
    StrRef labelRef = 0;
    if (!internString(label, labelRef)) {
      errorMessage_ = tr(STR_INVALID_FONT_MANIFEST);
      return false;
    }
    scriptGroupLabels_.push_back(labelRef);
  }

  families_.reserve(familiesArr.size());
  filteredIndices_.reserve(familiesArr.size());

  for (JsonObject fObj : familiesArr) {
    if (!FontInstaller::isValidFamilyName(fObj["name"] | "")) {
      errorMessage_ = tr(STR_INVALID_FONT_MANIFEST);
      return false;
    }
    ManifestFamily family;
    if (!internString(fObj["name"] | "", family.name) || !internString(fObj["description"] | "", family.description)) {
      errorMessage_ = tr(STR_INVALID_FONT_MANIFEST);
      return false;
    }

    for (JsonVariant script : fObj["scripts"].as<JsonArray>()) {
      const char* familyTag = script.as<const char*>();
      if (!familyTag) continue;
      for (size_t groupIndex = 0; groupIndex < scriptGroupLabels_.size(); groupIndex++) {
        JsonObject groupObj = groupsArr[groupIndex].as<JsonObject>();
        const char* groupTag = groupObj["tag"] | "";
        if (std::strcmp(familyTag, groupTag) == 0) {
          family.scriptMask |= uint32_t{1} << groupIndex;
          break;
        }
      }
    }

    // A directory with missing files is an incomplete installation; the size
    // checks below mark it for repair without retaining the whole SD registry.
    const char* familyRoot = SdCardFontRegistry::findFamilyRoot(str(family.name));
    family.installed = familyRoot != nullptr;

    family.fileStart = fileEntryCount_;
    for (JsonObject fileObj : fObj["files"].as<JsonArray>()) {
      const char* fileName = fileObj["name"] | "";
      const auto kind = fontdownload::classifyManifestFile(fileName);
      if (kind == fontdownload::ManifestFileKind::Invalid ||
          (kind == fontdownload::ManifestFileKind::Pack &&
           (fObj["files"].size() != 1 || !fontdownload::packMatchesFamily(fileName, str(family.name)) ||
            fileObj["size"].as<uint32_t>() > 128 * 1024 * 1024))) {
        errorMessage_ = tr(STR_INVALID_FONT_MANIFEST);
        return false;
      }
      ManifestFile file;
      if (!internString(fileObj["name"] | "", file.name)) {
        errorMessage_ = tr(STR_INVALID_FONT_MANIFEST);
        return false;
      }
      file.size = fileObj["size"] | 0u;

      if (!fileObj["crc32"].is<uint32_t>()) {
        LOG_ERR("FONT", "Malformed manifest file entry: missing or invalid crc32 for %s", str(file.name));
        errorMessage_ = tr(STR_INVALID_FONT_MANIFEST);
        return false;
      }
      file.crc32 = fileObj["crc32"].as<uint32_t>();

      // Preflight the complete destination path of every manifest file:
      // family and filename can each pass their own grammar and still not fit
      // once joined under the installed root. This runs for families that are
      // not installed and for every file after an early update detection, so a
      // manifest with one unbuildable path is rejected whole instead of
      // failing mid-transfer or creating a truncated path on SD.
      char path[FontInstaller::MAX_FONT_PATH_SIZE];
      if (kind == fontdownload::ManifestFileKind::SingleFont &&
          !FontInstaller::buildFontPath(str(family.name), str(file.name), path, sizeof(path))) {
        LOG_ERR("FONT", "Invalid font path in manifest: %s/%s", str(family.name), str(file.name));
        errorMessage_ = tr(STR_INVALID_FONT_MANIFEST);
        return false;
      }

      // Packs use their verified archive receipt; individual fonts keep their size check. A folder under the
      // store's name that no pack made is the user's own family: it shows as installed and is never overwritten.
      if (kind == fontdownload::ManifestFileKind::Pack) {
        family.hasUpdate = family.installed && FontPackInstaller::isPackFamily(familyRoot, str(family.name)) &&
                           !FontPackInstaller::matchesInstalled(familyRoot, str(family.name), file.size, file.crc32);
      } else if (family.installed && !family.hasUpdate) {
        HalFile f;
        if (Storage.openFileForRead("FONT", path, f)) {
          const size_t actual = f.fileSize();
          f.close();
          if (actual != file.size) family.hasUpdate = true;
        } else {
          // File missing on disk but family dir exists - treat as update
          family.hasUpdate = true;
        }
      }

      if (file.size > std::numeric_limits<uint32_t>::max() - family.totalSize) {
        LOG_ERR("FONT", "Manifest family size overflow for %s", str(family.name));
        errorMessage_ = tr(STR_INVALID_FONT_MANIFEST);
        return false;
      }
      family.totalSize += file.size;
      files_[fileEntryCount_++] = file;
    }
    family.fileCount = fileEntryCount_ - family.fileStart;

    families_.push_back(family);
  }

  LOG_DBG("FONT", "Manifest loaded: %zu families, %zu script groups", families_.size(), scriptGroupLabels_.size());
  return true;
}

// --- Download ---

void FontDownloadActivity::downloadAll() {
  downloadSelected(false);
}

void FontDownloadActivity::updateAll() {
  downloadSelected(true);
}

void FontDownloadActivity::downloadSelected(const bool updates) {
  HalPowerManager::Lock fullSpeed;
  {
    RenderLock lock(*this);
    cancelRequested_ = false;
    goHomeRequested_ = false;
    batchRunning_ = true;
    batchResult_ = false;
    batchTotalBytes_ = 0;
    batchDownloadedBytes_ = 0;
    batchFamilyIndex_ = 0;
    batchFamilyCount_ = 0;
    batchSuccessCount_ = 0;
    batchFailureCount_ = 0;
    terminalIdleTimerStarted_ = false;
    progressRenderGate_.reset();
    for (auto& family : families_) {
      family.failureReason = nullptr;
      family.requiredMb = 0;
    }
    for (const int familyIndex : filteredIndices_) {
      const auto& family = families_[familyIndex];
      if (updates ? !family.hasUpdate : family.installed || !family.chosen) continue;
      batchTotalBytes_ += family.totalSize;
      ++batchFamilyCount_;
    }
  }
  for (const int familyIndex : filteredIndices_) {
    auto& family = families_[familyIndex];
    if (updates ? !family.hasUpdate : family.installed || !family.chosen) continue;
    {
      RenderLock lock(*this);
      ++batchFamilyIndex_;
    }
    downloadFamily(family);
    RenderLock lock(*this);
    if (cancelRequested_) {
      batchRunning_ = false;
      return;
    }
    if (state_ == ERROR) {
      family.failureReason = failureReason_ ? failureReason_ : tr(STR_FONT_PACK_INSTALL_ERROR);
      family.requiredMb = requiredMb_;
      ++batchFailureCount_;
    } else {
      ++batchSuccessCount_;
    }
    state_ = DOWNLOADING;
  }
  waitForDownloadPaint();
  {
    RenderLock lock(*this);
    batchRunning_ = false;
    batchResult_ = true;
    resultFailureIndex_ = 0;
    while (resultFailureIndex_ < static_cast<int>(families_.size()) &&
           !families_[resultFailureIndex_].failureReason) ++resultFailureIndex_;
    state_ = COMPLETE;
  }
}

bool FontDownloadActivity::showDownloadAllRow() const {
  for (const int familyIndex : filteredIndices_) {
    if (!families_[familyIndex].installed) return true;
  }
  return false;
}

// Updates are done one font at a time from the store list; the add list holds only fonts not on the card.
bool FontDownloadActivity::showUpdateAllRow() const { return false; }

int FontDownloadActivity::specialRowCount() const {
  return (showDownloadAllRow() ? 1 : 0) + (showUpdateAllRow() ? 1 : 0);
}

bool FontDownloadActivity::isDownloadAllRow(int index) const { return showDownloadAllRow() && index == 0; }

bool FontDownloadActivity::isUpdateAllRow(int index) const {
  return showUpdateAllRow() && index == (showDownloadAllRow() ? 1 : 0);
}

int FontDownloadActivity::listItemCount() const {
  return filteredIndices_.empty() ? 0 : static_cast<int>(filteredIndices_.size()) + specialRowCount() + 1;
}

// The last row says how to add a font the store does not have; Up from the first row reaches it.
bool FontDownloadActivity::isOwnFontRow(const int index) const {
  return !filteredIndices_.empty() && index == listItemCount() - 1;
}

int FontDownloadActivity::listCount() const {
  switch (state_) {
    case STORE_LIST:
      return storeRowCount();
    case GROUP_LIST:
      return groupListItemCount();
    case FAMILY_LIST:
      return listItemCount();
    case WIFI_SELECTION:
    case LOADING_MANIFEST:
    case DOWNLOADING:
    case COMPLETE:
    case ERROR:
      return 0;
  }
  return 0;
}

int FontDownloadActivity::familyIndexFromList(const int listIndex) const {
  const int filteredIndex = listIndex - specialRowCount();
  if (filteredIndex < 0 || filteredIndex >= static_cast<int>(filteredIndices_.size())) return -1;
  return filteredIndices_[filteredIndex];
}

int FontDownloadActivity::groupMemberCount(const int scriptGroupIndex) const {
  if (scriptGroupIndex < 0 || scriptGroupIndex >= static_cast<int>(scriptGroupLabels_.size())) return 0;
  const uint32_t groupBit = uint32_t{1} << scriptGroupIndex;
  int count = 0;
  for (const auto& family : families_) {
    if (family.scriptMask & groupBit) count++;
  }
  return count;
}

void FontDownloadActivity::buildFilteredIndices(const int groupListIndex) {
  filteredIndices_.clear();
  filteredIndices_.reserve(families_.size());
  // The list adds fonts: one already on the card is in the store list, to update or delete there.
  if (groupListIndex <= 0) {
    for (int familyIndex = 0; familyIndex < static_cast<int>(families_.size()); familyIndex++) {
      if (!families_[familyIndex].installed) filteredIndices_.push_back(familyIndex);
    }
    return;
  }

  const uint32_t groupBit = uint32_t{1} << (groupListIndex - 1);
  for (int familyIndex = 0; familyIndex < static_cast<int>(families_.size()); familyIndex++) {
    if ((families_[familyIndex].scriptMask & groupBit) && !families_[familyIndex].installed)
      filteredIndices_.push_back(familyIndex);
  }
}

void FontDownloadActivity::enterGroup(const int groupListIndex) {
  closeRouting();
  buildFilteredIndices(groupListIndex);
  {
    RenderLock lock(*this);
    nav.reset();
    state_ = FAMILY_LIST;
    rowsDirty_ = true;
  }
}

int FontDownloadActivity::chosenCount() const {
  int count = 0;
  for (const int familyIndex : filteredIndices_) count += families_[familyIndex].chosen && !families_[familyIndex].installed;
  return count;
}

size_t FontDownloadActivity::totalDownloadSize() const {
  size_t total = 0;
  for (const int familyIndex : filteredIndices_) {
    const auto& family = families_[familyIndex];
    if (!family.installed && family.chosen) total += family.totalSize;
  }
  return total;
}

size_t FontDownloadActivity::totalUpdateSize() const {
  size_t total = 0;
  for (const int familyIndex : filteredIndices_) {
    if (families_[familyIndex].hasUpdate) total += families_[familyIndex].totalSize;
  }
  return total;
}

void FontDownloadActivity::updateDownloadProgress(size_t downloaded, size_t total) {
  mappedInput.update();
  if (mappedInput.isPressed(MappedInputManager::Button::Back) ||
      mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    cancelRequested_ = true;
  }
  if (mappedInput.wasLongPressed(MappedInputManager::Button::Back, 1000) || mappedInput.wasHomeGesture()) {
    cancelRequested_ = true;
    goHomeRequested_ = true;
  }
  const unsigned long now = millis();
  RenderLock lock(RenderLock::TryTake{});
  if (!lock.acquired()) return;
  fileProgress_ = downloaded;
  if (!progressRenderGate_.requestDue(downloaded, total ? total : fileTotal_, now)) return;
  lock.unlock();
  requestUpdate(true);
}

void FontDownloadActivity::waitForDownloadPaint() {
  unsigned long paintDelay = 0;
  {
    RenderLock lock(*this);
    paintDelay = progressRenderGate_.nextPaintDelay(millis());
  }
  if (paintDelay) delay(paintDelay);
}

bool FontDownloadActivity::prepareDownloadHeap() {
  RenderLock lock(*this);
#ifdef TENOR_PRESS_PROBE
  logFontDownloadHeap("before-release");
#endif
  std::vector<fui::ListItem>().swap(rowItems_);
  std::vector<std::string>().swap(rowLabels_);
  rowsDirty_ = true;
#ifdef TENOR_PRESS_PROBE
  logFontDownloadHeap("rows-released");
#endif
  releaseBaseSettingsList();
#ifdef TENOR_PRESS_PROBE
  logFontDownloadHeap("download-settings-released");
#endif
  if (auto* cache = renderer.getFontCacheManager()) cache->releaseSdFontCaches();
#ifdef TENOR_PRESS_PROBE
  logFontDownloadHeap("download-caches-released");
#endif
  sdFontSystem.releaseReaderForDownload(renderer);
#ifdef TENOR_PRESS_PROBE
  logFontDownloadHeap("download-reader-released");
  logFontDownloadHeap("before-tls-check");
#endif
  return fontdownload::hasTlsHeadroom(ESP.getFreeHeap(), ESP.getMaxAllocHeap(),
                                    HttpDownloader::MIN_TLS_FREE_HEAP, HttpDownloader::MIN_TLS_MAX_ALLOC);
}

void FontDownloadActivity::downloadFamily(ManifestFamily& family) {
  HalPowerManager::Lock fullSpeed;
  const bool wasInstalled = family.installed;
  {
    RenderLock lock(*this);
    state_ = DOWNLOADING;
    downloadingFamilyIndex_ = static_cast<int>(&family - families_.data());
    fileProgress_ = 0;
    fileTotal_ = family.fileCount ? files_[family.fileStart].size : 0;
    if (!batchRunning_) {
      progressRenderGate_.reset();
      cancelRequested_ = false;
      goHomeRequested_ = false;
      batchResult_ = false;
    }
    familyDownloadedBytes_ = 0;
    streamedBytes_ = 0;
    failureReason_ = nullptr;
    requiredMb_ = 0;
    installingPack_ = false;
    errorMessage_.clear();
    currentFileIndex_ = 0;
    currentFileTotal_ = family.fileCount;
  }
  // Check before touching the family directory so a failed update leaves the
  // installed family unchanged.
  if (!prepareDownloadHeap()) {
    LOG_ERR("FONT", "Low heap for download (%u free, %u max block)", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    RenderLock lock(*this);
    state_ = ERROR;
    failureReason_ = tr(STR_MEMORY_ERROR);
    errorMessage_ = failureReason_;
    return;
  }

  // The manifest was preflighted at parse time, but SD state can change
  // between parsing and download. Rebuild every destination before the family
  // directory is created so a rejected path cannot leave an empty directory
  // behind or start a transfer to a truncated target. One buffer is reused by
  // this preflight and by the per-file loop below.
  char destPath[FontInstaller::MAX_FONT_PATH_SIZE];
  const bool pack = family.fileCount == 1 &&
                    fontdownload::classifyManifestFile(str(files_[family.fileStart].name)) ==
                        fontdownload::ManifestFileKind::Pack;
  for (uint32_t i = 0; i < family.fileCount; i++) {
    const char* name = str(files_[family.fileStart + i].name);
    if (pack ? !fontdownload::packMatchesFamily(name, str(family.name))
             : !FontInstaller::buildFontPath(str(family.name), name, destPath, sizeof(destPath))) {
      LOG_ERR("FONT", "Invalid font path: %s/%s", str(family.name), str(files_[family.fileStart + i].name));
      RenderLock lock(*this);
      state_ = ERROR;
      failureReason_ = tr(STR_INVALID_FONT_MANIFEST);
      errorMessage_ = failureReason_;
      return;
    }
  }

  if (!pack && !fontInstaller_.ensureFamilyDir(str(family.name))) {
    RenderLock lock(*this);
    state_ = ERROR;
    failureReason_ = tr(STR_FONT_PACK_IO_ERROR);
    errorMessage_ = failureReason_;
    return;
  }

  for (uint32_t i = 0; i < family.fileCount; i++) {
    const ManifestFile& file = files_[family.fileStart + i];
    waitForDownloadPaint();
    {
      RenderLock lock(*this);
      fileProgress_ = 0;
      fileTotal_ = file.size;
      streamedBytes_ = 0;
      progressRenderGate_.reset();
      progressRenderGate_.requestDue(0, file.size, millis());
    }
    requestUpdateAndWait();

    // Rebuilt into the same buffer per file: the path may only become
    // unbuildable if storage changed since the preflight above, and that must
    // fail closed rather than download to a truncated target.
    if (!pack && !FontInstaller::buildFontPath(str(family.name), str(file.name), destPath, sizeof(destPath))) {
      LOG_ERR("FONT", "Invalid font path: %s/%s", str(family.name), str(file.name));
      RenderLock lock(*this);
      state_ = ERROR;
      failureReason_ = tr(STR_INVALID_FONT_MANIFEST);
      errorMessage_ = failureReason_;
      return;
    }

    downloadUrl_.assign(baseUrl_).append(str(file.name));

    const auto fetch = [&](const auto& onData) {
      size_t received = 0;
      uint32_t parts = 0, retries = 0;
      HttpDownloader::RangeSession session;
      return http_range::transfer(file.size, received, cancelRequested_,
          [&](size_t first, size_t last, bool& whole, bool& stop) {
#ifdef TENOR_PRESS_PROBE
        if (session.hasTlsContext()) logFontDownloadHeap("before-tls-reuse");
#endif
        if (!session.hasTlsContext() && !prepareDownloadHeap()) {
          RenderLock lock(*this);
          failureReason_ = tr(STR_MEMORY_ERROR);
          stop = true;
          return false;
        }
        const auto countedData = [&](const uint8_t* bytes, const size_t count) {
          const auto written = onData(bytes, count);
          received += written.bytes;
          streamedBytes_ += static_cast<uint32_t>(written.bytes);
          stop = written.fatal;
          return written.complete;
        };
        HttpDownloader::TransferStats part;
        const bool ok = HttpDownloader::fetchRange(downloadUrl_, first, last, countedData, nullptr,
            [&](size_t, size_t) { updateDownloadProgress(received, file.size); },
            &cancelRequested_, &part, &whole, &session);
        updateDownloadProgress(received, file.size);
#ifdef TENOR_PRESS_PROBE
        LOG_INF("FONT_RANGE", "part=%u first=%u bytes=%u written=%u status=%d ok=%d free=%u largest=%u min=%u retries=%u stop=%d",
                parts + 1, static_cast<unsigned>(first), part.bytes, static_cast<unsigned>(received),
                part.status, ok, ESP.getFreeHeap(), ESP.getMaxAllocHeap(), ESP.getMinFreeHeap(), retries, stop);
        if (!ok && !cancelRequested_ && !stop && !whole && retries < http_range::MAX_PART_RETRIES)
          LOG_INF("FONT_RANGE", "retry=%u first=%u", retries + 1, static_cast<unsigned>(received));
#endif
        return ok;
      }, parts, retries);
    };
    auto transfer = fontdownload::TransferResult::OK;
    if (pack) {
      LOG_INF("FONT", "Pack download bytes=%u free=%u largest=%u min=%u", file.size, ESP.getFreeHeap(),
              ESP.getMaxAllocHeap(), ESP.getMinFreeHeap());
      transfer = fontdownload::downloadAndInstallPack(
          str(file.name), file.size, file.crc32, cancelRequested_, fetch,
          [&](const char* path) {
            waitForDownloadPaint();
            {
              RenderLock lock(*this);
              installingPack_ = true;
              fileProgress_ = file.size;
              progressRenderGate_.reset();
              progressRenderGate_.requestDue(fileProgress_, fileTotal_, millis());
            }
            requestUpdateAndWait();
            return installDownloadedPack(path, file.size, file.crc32);
          });
    } else {
      if (!fontInstaller_.ensureFontDir(str(family.name), str(file.name))) {
        transfer = fontdownload::TransferResult::IoError;
      } else {
        bool backupCleanupPending = false;
        const auto installed = webdav::installVerifiedFile(
            Storage, destPath,
            [&](const char* staging) {
              transfer = fontdownload::streamVerifiedFile(staging, file.size, file.crc32, cancelRequested_, fetch);
              return transfer == fontdownload::TransferResult::OK;
            },
            [&](const char* staging) { return fontInstaller_.validateCpfontFile(staging); }, &backupCleanupPending);
        if (installed != webdav::InstallResult::OK && transfer == fontdownload::TransferResult::OK)
          transfer = fontdownload::TransferResult::IoError;
        if (backupCleanupPending) LOG_ERR("FONT", "Committed font %s; backup retained", destPath);
      }
    }
    {
      RenderLock lock(*this);
      if (batchRunning_) batchDownloadedBytes_ += streamedBytes_;
      familyDownloadedBytes_ += streamedBytes_;
      fileProgress_ = 0;
    }
    if (transfer != fontdownload::TransferResult::OK) {
      family.installed = wasInstalled;
      family.hasUpdate = wasInstalled;
      if (transfer == fontdownload::TransferResult::Aborted) {
        if (goHomeRequested_) {
          onGoHome();
          return;
        }
        returnToFamilyList(fontdownload::ReleaseButton::Back);
        return;
      }
      LOG_ERR("FONT", "Font download failed name=%s result=%d", str(file.name), static_cast<int>(transfer));
      RenderLock lock(*this);
      state_ = ERROR;
      switch (transfer) {
        case fontdownload::TransferResult::NoSpace: {
          char message[64];
          const auto mb = static_cast<unsigned>((fontdownload::requiredPackSpace(file.size) + 1024 * 1024 - 1) /
                                                (1024 * 1024));
          std::snprintf(message, sizeof(message), tr(STR_FONT_PACK_NO_SPACE), mb);
          failureReason_ = tr(STR_FONT_PACK_NO_SPACE);
          requiredMb_ = mb;
          errorMessage_ = message;
          break;
        }
        case fontdownload::TransferResult::ChecksumError:
          failureReason_ = tr(STR_FONT_PACK_CHECKSUM_ERROR);
          break;
        case fontdownload::TransferResult::NetworkError:
          if (!failureReason_) failureReason_ = tr(STR_FONT_PACK_TRANSFER_ERROR);
          break;
        case fontdownload::TransferResult::InstallError:
          if (!failureReason_) failureReason_ = tr(STR_FONT_PACK_INSTALL_ERROR);
          break;
        default:
          failureReason_ = tr(STR_FONT_PACK_IO_ERROR);
          break;
      }
      if (transfer != fontdownload::TransferResult::NoSpace) errorMessage_ = failureReason_;
      return;
    }
    LOG_DBG("FONT", "Downloaded %s (size=%u crc32=%08x)", str(file.name), file.size, file.crc32);

    currentFileIndex_++;
  }

  waitForDownloadPaint();
  {
    RenderLock lock(*this);
    sdFontSystem.markRegistryDirty();
    family.installed = true;
    family.chosen = false;  // a later delete shows it off again
    family.hasUpdate = false;
    installingPack_ = false;
    state_ = batchRunning_ ? DOWNLOADING : COMPLETE;
  }
}

void FontDownloadActivity::promptDeleteSelectedFamily() {
  const int pendingDeleteFamilyIndex = familyIndexFromList(nav.selected);
  if (pendingDeleteFamilyIndex < 0 || pendingDeleteFamilyIndex >= static_cast<int>(families_.size())) {
    return;
  }

  std::string heading = tr(STR_DELETE);
  const auto& family = families_[pendingDeleteFamilyIndex];
  std::string body = str(family.name);
  startActivityForResult(
      std::make_unique<ConfirmationActivity>(
          renderer, mappedInput, heading, body,
          ConfirmationContent{tr(STR_ALERT_FONT_QUESTION), body + " " + tr(STR_ALERT_FONT_MESSAGE_SUFFIX),
                              tr(STR_ALERT_FONT_ACTION_SHORT), tr(STR_ALERT_FONT_ACTION_LONG), "", true}),
      [this](const ActivityResult& result) { onDeleteConfirmationResult(result); });
}

void FontDownloadActivity::onDeleteConfirmationResult(const ActivityResult& result) {
  if (result.isCancelled) {
    requestUpdate();
    return;
  }

  const int familyIndex = familyIndexFromList(nav.selected);
  if (familyIndex < 0) {
    requestUpdate();
    return;
  }
  auto& family = families_[familyIndex];

  if (fontInstaller_.deleteFamily(str(family.name)) != FontInstaller::Error::OK) {
    RenderLock lock(*this);
    state_ = ERROR;
    errorMessage_ = tr(STR_FONT_DELETE_FAILED);
  } else {
    RenderLock lock(*this);  // the render task reads these rows
    sdFontSystem.markRegistryDirty();
    family.installed = false;
    family.chosen = false;
    family.hasUpdate = false;
    // Unlike the other family_ mutations, this one stays in FAMILY_LIST (no
    // state_ transition to hang the rebuild off), so it must set the flag
    // directly.
    rowsDirty_ = true;
  }

  requestUpdate();
}

bool FontDownloadActivity::isSelectedFamilyDeletable() const {
  const int familyIndex = familyIndexFromList(nav.selected);  // -1 on the special rows and the own-font row
  if (familyIndex < 0) return false;
  const auto& family = families_[familyIndex];
  return family.installed && !family.hasUpdate;
}

void FontDownloadActivity::activateSelected() {
  if (filteredIndices_.empty()) return;
  if (isDownloadAllRow(nav.selected)) {
    // Nothing switched on: the row only says to pick families (the whole store is hundreds of MB).
    if (chosenCount() == 0) return;
    currentFileIndex_ = 0;
    currentFileTotal_ = 0;
    for (const int familyIndex : filteredIndices_) {
      const auto& family = families_[familyIndex];
      if (!family.installed && family.chosen) currentFileTotal_ += family.fileCount;
    }
    downloadAll();
  } else if (isUpdateAllRow(nav.selected)) {
    currentFileIndex_ = 0;
    currentFileTotal_ = 0;
    for (const int familyIndex : filteredIndices_) {
      if (families_[familyIndex].hasUpdate) currentFileTotal_ += families_[familyIndex].fileCount;
    }
    updateAll();
  } else {
    // The special rows disappear when a download starts, so a stale selection
    // can map past the family table.
    const int familyIndex = familyIndexFromList(nav.selected);
    if (familyIndex < 0 || familyIndex >= static_cast<int>(families_.size())) return;
    auto& family = families_[familyIndex];
    if (!family.installed) {
      {
        RenderLock lock(*this);
        family.chosen = !family.chosen;
        rowsDirty_ = true;
      }
      requestUpdate();  // a button press does not repaint the list by itself (touch does)
      return;
    }
    if (family.hasUpdate) {
      currentFileIndex_ = 0;
      currentFileTotal_ = family.fileCount;
      downloadFamily(family);
    } else {
      promptDeleteSelectedFamily();
      return;
    }
  }
  requestUpdateAndWait();
}

void FontDownloadActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the GUI.drawHeader band, above the button hints.
  if (x3settings::beginScreen(screen, renderer)) {
    tabBandDrawn = true;
  } else {
    screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                  static_cast<int16_t>(metrics.buttonHintsHeight), 0});
    screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  }

  if (state_ == FAMILY_LIST && filteredIndices_.empty()) {
    if (tenorchrome::x3ScreenLayoutEnabled())
      tenorchrome::drawInfoInRect(renderer, tr(STR_ALL_FONTS_INSTALLED), tenorchrome::x3ScreenLayout(renderer).bodyRect);
    else screen.centeredText(I18N.get(shell::uglyParts() ? StrId::STR_UGLY_FONT_EMPTY : StrId::STR_ALL_FONTS_INSTALLED),
                              screen.theme().bodyText);
    return;
  }

  if (rowsDirty_) {
    rebuildRowItems();
    rowsDirty_ = false;
  }

  fui::ListProps props;
  props.items = rowItems_.data();
  props.count = static_cast<uint16_t>(rowItems_.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;               // air between the status and the row edge
  syncListViewport(screen, props);
  screen.list(props);
}

void FontDownloadActivity::rebuildRowItems() {
  switch (state_) {
    case STORE_LIST:
      rebuildStoreRowItems();
      return;
    case GROUP_LIST:
      rebuildGroupRowItems();
      return;
    case FAMILY_LIST:
      rebuildFamilyRowItems();
      return;
    case WIFI_SELECTION:
    case LOADING_MANIFEST:
    case DOWNLOADING:
    case COMPLETE:
    case ERROR:
      rowLabels_.clear();
      rowItems_.clear();
      return;
  }
}

// --- The store list: fonts on the card ---

// Row 0 adds fonts, then the fonts on the card, then the way to add your own.
int FontDownloadActivity::storeRowCount() const { return static_cast<int>(installed_.size()) + 2; }

void FontDownloadActivity::rebuildStoreRowItems() {
  const int listSize = storeRowCount();
  rowLabels_.assign(listSize, std::string());
  rowItems_.clear();
  rowItems_.reserve(listSize);
  for (int i = 0; i < listSize; i++) {
    fui::ListItem item;
    if (i == 0) {
      item.label = tr(STR_ADD_NEW_FONTS);
      item.subtitle = tr(STR_ADD_NEW_FONTS_SUB);
      item.opensNext = true;
    } else if (i == listSize - 1) {
      rowLabels_[i] = std::string(tr(STR_FILE_TRANSFER)) + " > " + tr(STR_JOIN_NETWORK) + tr(STR_OWN_FONT_HINT);
      item.label = tr(STR_OWN_FONT);
      item.subtitle = rowLabels_[i].c_str();
    } else {
      const auto& font = installed_[i - 1];
      item.label = font.name.c_str();
      item.value = font.hasUpdate ? tr(STR_UPDATE_AVAILABLE) : tr(STR_INSTALLED);
    }
    item.actionValue = static_cast<int16_t>(i);
    rowItems_.push_back(item);
  }
}

void FontDownloadActivity::activateStoreRow(const int index) {
  if (index == 0) {
    updateTarget_.clear();
    beginNetwork();
    return;
  }
  if (index >= 1 && index <= static_cast<int>(installed_.size())) showInstalledMenu(index - 1);
}

// A font on the card: Delete, and Update when the store has a newer pack.
void FontDownloadActivity::showInstalledMenu(const int installedIndex) {
  if (installedIndex < 0 || installedIndex >= static_cast<int>(installed_.size())) return;
  const bool update = installed_[installedIndex].hasUpdate;
  const char* options[2];
  int count = 0;
  if (update) options[count++] = tr(STR_UPDATE);
  options[count++] = tr(STR_ALERT_FONT_ACTION_LONG);
  const std::string name = installed_[installedIndex].name;
  rowMenu.show(tr(STR_FONT_BOX_TITLE), name.c_str(), options, count, -1, [this, installedIndex, update](const int choice) {
    if (installedIndex >= static_cast<int>(installed_.size())) return;
    if (update && choice == 0) {
      startUpdate(installed_[installedIndex].name);
    } else {
      promptDeleteInstalled(installedIndex);
    }
  });
  requestUpdate();
}

void FontDownloadActivity::startUpdate(const std::string& name) {
  updateTarget_ = name;
  beginNetwork();
}

void FontDownloadActivity::promptDeleteInstalled(const int installedIndex) {
  pendingDeleteName_ = installed_[installedIndex].name;
  startActivityForResult(
      std::make_unique<ConfirmationActivity>(
          renderer, mappedInput, std::string(tr(STR_DELETE)), pendingDeleteName_,
          ConfirmationContent{tr(STR_ALERT_FONT_QUESTION),
                              pendingDeleteName_ + " " + tr(STR_ALERT_FONT_MESSAGE_SUFFIX),
                              tr(STR_ALERT_FONT_ACTION_SHORT), tr(STR_ALERT_FONT_ACTION_LONG), "", true}),
      [this](const ActivityResult& result) { onDeleteInstalledResult(result); });
}

// The font the reader draws with now (its own pack, or the pack standing in for a built-in face at a heavier
// weight): its files are open in the font manager, so the unit restarts after it is deleted.
bool FontDownloadActivity::fontIsLoaded(const std::string& name) {
  if (name == SETTINGS.sdFontFamilyName) return true;
  return SETTINGS.sdFontFamilyName[0] == '\0' && SETTINGS.readerInkWeight != readerInk::ZERO &&
         (name == "NotoSans" || name == "NotoSerif");
}

void FontDownloadActivity::onDeleteInstalledResult(const ActivityResult& result) {
  const std::string name = std::move(pendingDeleteName_);
  pendingDeleteName_.clear();
  if (result.isCancelled) {
    requestUpdate();
    return;
  }
  const bool loaded = fontIsLoaded(name);
  if (fontInstaller_.deleteFamily(name.c_str()) != FontInstaller::Error::OK) {
    RenderLock lock(*this);
    errorMessage_ = tr(STR_FONT_DELETE_FAILED);
    downloadingFamilyIndex_ = -1;
    state_ = ERROR;
    requestUpdate();
    return;
  }
  sdFontSystem.markRegistryDirty();
  if (loaded) {
    // The font manager still holds the deleted files; a clean start is the safe way to let go of them.
    networkUsed_ = true;
    finish();
    return;
  }
  loadInstalledList();
  {
    RenderLock lock(*this);
    nav.selected = std::min(nav.selected.load(), storeRowCount() - 1);
  }
  requestUpdate();
}

void FontDownloadActivity::rebuildGroupRowItems() {
  const int listSize = groupListItemCount();
  rowLabels_.assign(listSize, std::string());
  rowItems_.clear();
  rowItems_.reserve(listSize);
  for (int rowIndex = 0; rowIndex < listSize; rowIndex++) {
    fui::ListItem item;
    item.label = rowIndex == 0 ? tr(STR_ALL_FONTS) : str(scriptGroupLabels_[rowIndex - 1]);
    const int memberCount = rowIndex == 0 ? static_cast<int>(families_.size()) : groupMemberCount(rowIndex - 1);
    rowLabels_[rowIndex] = std::to_string(memberCount);
    item.value = rowLabels_[rowIndex].c_str();
    item.actionValue = static_cast<int16_t>(rowIndex);
    rowItems_.push_back(item);
  }
}

void FontDownloadActivity::rebuildFamilyRowItems() {
  const int listSize = listItemCount();
  rowLabels_.assign(listSize, std::string());
  rowItems_.clear();
  rowItems_.reserve(listSize);
  for (int i = 0; i < listSize; i++) {
    fui::ListItem item;
    if (isDownloadAllRow(i)) {
      const int chosen = chosenCount();
      if (chosen) {
        char label[96];
        std::snprintf(label, sizeof(label), tr(STR_DOWNLOAD_CHOSEN), chosen, formatSize(totalDownloadSize()).c_str());
        rowLabels_[i] = label;
        item.label = rowLabels_[i].c_str();
      } else {
        item.label = tr(STR_PICK_FONTS_TO_DOWNLOAD);
        item.state = fui::StateDisabled;
      }
    } else if (isOwnFontRow(i)) {
      // The way there, from the screens' own names, so a renamed screen cannot leave the hint stale.
      rowLabels_[i] = std::string(tr(STR_FILE_TRANSFER)) + " > " + tr(STR_JOIN_NETWORK) + tr(STR_OWN_FONT_HINT);
      item.label = tr(STR_OWN_FONT);
      item.subtitle = rowLabels_[i].c_str();
    } else if (isUpdateAllRow(i)) {
      rowLabels_[i] = std::string(tr(STR_UPDATE_ALL)) + " (" + formatSize(totalUpdateSize()) + ")";
      item.label = rowLabels_[i].c_str();
    } else {
      const auto& family = families_[familyIndexFromList(i)];
      item.label = str(family.name);
      if (family.description != 0) {
        item.subtitle = str(family.description);
      } else if (!family.installed || family.hasUpdate) {
        rowLabels_[i] = formatSize(family.totalSize);  // what switching it on adds to the download
        item.subtitle = rowLabels_[i].c_str();
      }
      if (family.hasUpdate) {
        item.value = tr(STR_UPDATE_AVAILABLE);
      } else if (family.installed) {
        item.value = tr(STR_INSTALLED);
        // Dimmed but still tappable (opens the delete prompt): visual-only
        // disabled state, the row stays enabled for hit registration.
        item.state = fui::StateDisabled;
      } else {
        item.toggle = true;
        item.check = true;  // pick several: a check box, not a switch
        item.toggleChecked = family.chosen;
      }
    }
    item.actionValue = static_cast<int16_t>(i);
    rowItems_.push_back(item);
  }
}

// --- Input handling ---

bool FontDownloadActivity::handleCustomInput() {
  if (state_ == STORE_LIST || state_ == GROUP_LIST || state_ == FAMILY_LIST) {
    // The base list protocol (Back/Confirm, touch routing, swipe scroll,
    // button navigation) handles both list states.
    return false;
  }

  if (tenorchrome::x3ScreenLayoutEnabled() && !(state_ == COMPLETE && batchResult_) && infoPages_ > 1 &&
      (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
       mappedInput.wasPressed(MappedInputManager::Button::Down))) {
    RenderLock lock(*this);
    infoPage_ = std::clamp(infoPage_ + (mappedInput.wasPressed(MappedInputManager::Button::Down) ? 1 : -1),
                            0, infoPages_ - 1);
    terminalStateSince_ = millis();
    requestUpdate();
    return true;
  }

  if (state_ == COMPLETE || state_ == ERROR) {
    if (!terminalIdleTimerStarted_) {
      terminalStateSince_ = millis();
      terminalIdleTimerStarted_ = true;
    } else if (terminalStateIdleExpired(millis())) {
      finish();
      return true;
    }
  }

  if (state_ == COMPLETE) {
    if (batchResult_ && batchFailureCount_ > 1) {
      int swipeDistance = 0;
      unsigned long swipeDuration = 0;
      const bool swiped = mappedInput.wasVerticalSwipe(swipeDistance, swipeDuration);
      const bool previous = mappedInput.wasReleased(MappedInputManager::Button::Up) ||
                            mappedInput.wasReleased(MappedInputManager::Button::Left) ||
                            (swiped && swipeDistance > 0);
      const bool next = mappedInput.wasReleased(MappedInputManager::Button::Down) ||
                        mappedInput.wasReleased(MappedInputManager::Button::Right) ||
                        (swiped && swipeDistance < 0);
      if (previous || next) {
        const int direction = previous ? -1 : 1;
        int index = resultFailureIndex_ + direction;
        while (index >= 0 && index < static_cast<int>(families_.size()) && !families_[index].failureReason)
          index += direction;
        if (index >= 0 && index < static_cast<int>(families_.size())) {
          resultFailureIndex_ = index;
          infoPage_ = 0;
        }
        terminalStateSince_ = millis();
        requestUpdate();
        return true;
      }
    }
    int x = 0;
    int y = 0;
    const bool backPressed = mappedInput.wasPressed(MappedInputManager::Button::Back);
    const bool confirmPressed = !backPressed && mappedInput.wasPressed(MappedInputManager::Button::Confirm);
    const bool screenTapped = !backPressed && !confirmPressed && mappedInput.wasScreenTapped(x, y);
    if (backPressed || confirmPressed || screenTapped) {
      returnToFamilyList(backPressed   ? fontdownload::ReleaseButton::Back
                         : confirmPressed ? fontdownload::ReleaseButton::Confirm
                                          : fontdownload::ReleaseButton::None);
    }
  } else if (state_ == ERROR) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      returnToFamilyList(fontdownload::ReleaseButton::Back);
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      if (downloadingFamilyIndex_ >= 0 && downloadingFamilyIndex_ < static_cast<int>(families_.size())) {
        terminalIdleTimerStarted_ = false;
        downloadFamily(families_[downloadingFamilyIndex_]);
        requestUpdateAndWait();
        return true;
      } else {
        returnToFamilyList(fontdownload::ReleaseButton::Confirm);
      }
    } else {
      int x = 0;
      int y = 0;
      if (mappedInput.wasScreenTapped(x, y)) {
        if (downloadingFamilyIndex_ >= 0 && downloadingFamilyIndex_ < static_cast<int>(families_.size())) {
          terminalIdleTimerStarted_ = false;
          downloadFamily(families_[downloadingFamilyIndex_]);
          requestUpdateAndWait();
          return true;
        }
        returnToFamilyList(fontdownload::ReleaseButton::None);
      }
    }
  }

  return true;
}

bool FontDownloadActivity::handleButtons() {
  using Button = MappedInputManager::Button;
  switch (familyListReleaseGuard_.button()) {
    case fontdownload::ReleaseButton::Back:
      if (familyListReleaseGuard_.consumeIfReleased(mappedInput.wasReleased(Button::Back),
                                                     mappedInput.isPressed(Button::Back))) {
        return true;
      }
      break;
    case fontdownload::ReleaseButton::Confirm:
      if (familyListReleaseGuard_.consumeIfReleased(mappedInput.wasReleased(Button::Confirm),
                                                     mappedInput.isPressed(Button::Confirm))) {
        return true;
      }
      break;
    case fontdownload::ReleaseButton::None:
      break;
  }
  return UiListActivity::handleButtons();
}

bool FontDownloadActivity::terminalStateIdleExpired(const unsigned long now) const {
  return terminalIdleTimerStarted_ && now - terminalStateSince_ >= TERMINAL_IDLE_TIMEOUT_MS;
}

void FontDownloadActivity::returnToFamilyList(const fontdownload::ReleaseButton releaseButton) {
  if (!updateTarget_.empty()) {  // one font was updated: nothing to go back to but the store, via the restart
    finish();
    return;
  }
  familyListReleaseGuard_.arm(releaseButton);
  {
    RenderLock lock(*this);
    state_ = FAMILY_LIST;
    rowsDirty_ = true;
  }
  terminalIdleTimerStarted_ = false;
  requestUpdate();
}

// --- Rendering ---

std::string FontDownloadActivity::formatSize(uint64_t bytes) {
  char buf[32];
  if (bytes >= 1024 * 1024) {
    snprintf(buf, sizeof(buf), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
  } else if (bytes >= 1024) {
    snprintf(buf, sizeof(buf), "%.0f KB", static_cast<double>(bytes) / 1024.0);
  } else {
    snprintf(buf, sizeof(buf), "%zu B", bytes);
  }
  return buf;
}

// The waits of this screen as notes. The two lists wait for the shared ugly list.
bool FontDownloadActivity::renderUglyNote() const {
  if (state_ == DOWNLOADING || (state_ == COMPLETE && batchResult_)) return false;
  char line[160];
  std::string detail;
  int percent = -1;
  ugly::Hints hints;
  hints.back = true;
  switch (state_) {
    case LOADING_MANIFEST:
      snprintf(line, sizeof(line), "%s", tr(STR_UGLY_FONT_LOADING));
      hints.back = false;
      break;
    case DOWNLOADING:
      if (installingPack_) {
        snprintf(line, sizeof(line), "%s", tr(STR_FONT_PACK_INSTALLING));
        hints.back = false;
      } else {
        snprintf(line, sizeof(line), tr(STR_UGLY_FONT_DOWNLOADING), str(families_[downloadingFamilyIndex_].name),
                 static_cast<int>(currentFileIndex_ + 1), static_cast<int>(currentFileTotal_));
        detail = formatSize(fileProgress_) + " / " + formatSize(fileTotal_);
      }
      if (!installingPack_ && fileTotal_ > 0) {
        percent = static_cast<int>(static_cast<uint64_t>(fileProgress_) * 100 / fileTotal_);
      }
      break;
    case COMPLETE:
      snprintf(line, sizeof(line), "%s", tr(STR_UGLY_FONT_DONE));
      break;
    case ERROR:
      snprintf(line, sizeof(line), "%s", tr(STR_UGLY_FONT_FAILED));
      detail = errorMessage_;
      hints.confirm = true;
      break;
    default:
      return false;
  }
  ugly::notePage(renderer, mappedInput, tr(STR_FONT_BROWSER), line, detail.empty() ? nullptr : detail.c_str(), percent,
                 hints);
  return true;
}

const char* FontDownloadActivity::headerSubtitle() {
  if (state_ == FAMILY_LIST && !hasGroupScreen()) return tr(STR_ADD_NEW_FONTS);
  if (state_ != FAMILY_LIST || !hasGroupScreen()) return nullptr;
  const int scriptGroupIndex = groupNav_.selected - 1;
  return scriptGroupIndex >= 0 && scriptGroupIndex < static_cast<int>(scriptGroupLabels_.size())
             ? str(scriptGroupLabels_[scriptGroupIndex])
             : tr(STR_ALL_FONTS);
}

void FontDownloadActivity::drawChrome() {
  if (tenorchrome::x3ScreenLayoutEnabled()) {
    std::string title = tr(STR_FONT_BROWSER);
    if (headerSubtitle() && *headerSubtitle()) title += std::string(" / ") + headerSubtitle();
    x3settings::drawPath(renderer, title.c_str());
    return;
  }
  const auto& metrics = UITheme::getInstance().getMetrics();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight},
                 tr(STR_FONT_BROWSER), headerSubtitle());
}

void FontDownloadActivity::drawListHints() {
  if (state_ == GROUP_LIST) {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    return;
  }
  if (state_ == STORE_LIST) {
    const int last = storeRowCount() - 1;
    const char* confirm = nav.selected == 0 ? tr(STR_OPEN) : nav.selected == last ? "" : tr(STR_SELECT);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirm, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    return;
  }
  const bool hasVisibleFamilies = !filteredIndices_.empty();
  const int familyIndex = familyIndexFromList(nav.selected);
  const bool toggles = familyIndex >= 0 && !families_[familyIndex].installed;
  const char* confirmLabel = !hasVisibleFamilies || isOwnFontRow(nav.selected) ||
                                     (isDownloadAllRow(nav.selected) && chosenCount() == 0)
                                 ? ""
                             : isSelectedFamilyDeletable()  ? tr(STR_DELETE)
                             : isUpdateAllRow(nav.selected) ? tr(STR_UPDATE)
                             : toggles                      ? tr(STR_TOGGLE)
                                                            : tr(STR_DOWNLOAD);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, hasVisibleFamilies ? tr(STR_DIR_UP) : "",
                                            hasVisibleFamilies ? tr(STR_DIR_DOWN) : "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void FontDownloadActivity::render(RenderLock&&) {
  if (rowMenu.processRender(renderer, mappedInput)) return;
  if (state_ == DOWNLOADING || (state_ == COMPLETE && batchResult_)) {
    const auto paintDelay = progressRenderGate_.paintDelay(millis());
    if (paintDelay) delay(paintDelay);
    progressRenderGate_.paintStarted(millis());
  } else {
    progressRenderGate_.renderStarted();
  }
  if (shell::uglyParts() && renderUglyNote()) return;
  if (tenorchrome::x3ScreenLayoutEnabled() && state_ != STORE_LIST && state_ != GROUP_LIST && state_ != FAMILY_LIST) {
    renderX3State();
    return;
  }
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  // The two lists by hand: the layout keeps its words and the shared skin writes them.
  if ((state_ == STORE_LIST || state_ == GROUP_LIST || state_ == FAMILY_LIST) && renderUglyList()) {
    renderer.displayBuffer();
    return;
  }

  renderer.clearScreen();

  const char* headerSubtitle = this->headerSubtitle();
  drawChrome();

  const auto lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const auto contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const auto centerY = (pageHeight - lineHeight) / 2;
  const auto renderFontList = [&] {
    bool firstPass = true;
    renderSettledList(activeNav(), [&] {
      if (!firstPass) {
        renderer.clearScreen();
        if (tenorchrome::x3ScreenLayoutEnabled()) drawChrome();
        else GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_FONT_BROWSER),
                              headerSubtitle);
      }
      firstPass = false;
      renderUi();
    });
  };

  if (state_ == LOADING_MANIFEST) {
    renderer.drawCenteredText(UI_10_FONT_ID, centerY, tr(STR_LOADING_FONT_LIST));
  } else if (state_ == GROUP_LIST || state_ == STORE_LIST) {
    renderFontList();
    drawListHints();
  } else if (state_ == FAMILY_LIST) {
    renderFontList();
    drawListHints();
  } else if (state_ == DOWNLOADING) {
    const auto& family = families_[downloadingFamilyIndex_];
    const auto familyBytes = uint64_t{familyDownloadedBytes_} + fileProgress_;
    const auto totalBytes = batchRunning_ ? batchTotalBytes_ : family.totalSize;
    const auto downloadedBytes = batchRunning_ ? batchDownloadedBytes_ + fileProgress_ : familyBytes;
    const unsigned currentPercent = family.totalSize ? static_cast<unsigned>(familyBytes * 100 / family.totalSize) : 0;
    const unsigned totalPercent = totalBytes ? static_cast<unsigned>(downloadedBytes * 100 / totalBytes) : 0;
    const auto rows = fontdownload::transferLayout(centerY, lineHeight, metrics.verticalSpacing,
                                                   metrics.progressBarHeight, BaseTheme::PROGRESS_LABEL_GAP);
    const int width = pageWidth - metrics.contentSidePadding * 2;
    const auto name = renderer.truncatedText(UI_10_FONT_ID, str(family.name), width);
    renderer.drawCenteredText(UI_10_FONT_ID, rows.name, name.c_str());
    char status[128];
    if (installingPack_) {
      std::snprintf(status, sizeof(status), "%s", tr(STR_FONT_PACK_INSTALLING));
    } else {
      std::snprintf(status, sizeof(status), tr(STR_FONT_BATCH_DOWNLOADING),
                    formatSize(familyBytes).c_str(), formatSize(family.totalSize).c_str());
    }
    renderer.drawCenteredText(UI_10_FONT_ID, rows.status, status);
    GUI.drawProgressBar(renderer, Rect{metrics.contentSidePadding, rows.bar1, width, metrics.progressBarHeight},
                        std::min(currentPercent, 100u), 100);
    std::snprintf(status, sizeof(status), tr(STR_FONT_BATCH_TOTAL), batchRunning_ ? batchFamilyIndex_ : 1u,
                  batchRunning_ ? batchFamilyCount_ : 1u, totalPercent);
    renderer.drawCenteredText(UI_10_FONT_ID, rows.total, status);
    GUI.drawProgressBar(renderer, Rect{metrics.contentSidePadding, rows.bar2, width, metrics.progressBarHeight},
                        std::min(totalPercent, 100u), 100);
    const auto sizeText = formatSize(downloadedBytes) + " / " + formatSize(totalBytes);
    renderer.drawCenteredText(UI_10_FONT_ID, rows.size, sizeText.c_str());
    const auto labels = mappedInput.mapLabels(installingPack_ ? "" : tr(STR_CANCEL), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state_ == COMPLETE) {
    if (batchResult_) {
      char summary[96];
      std::snprintf(summary, sizeof(summary), tr(STR_FONT_BATCH_DONE), batchSuccessCount_, batchFamilyCount_);
      renderer.drawCenteredText(UI_10_FONT_ID, centerY - 2 * lineHeight, summary, true, EpdFontFamily::BOLD);
      if (batchFailureCount_ && resultFailureIndex_ < static_cast<int>(families_.size())) {
        const auto& failed = families_[resultFailureIndex_];
        unsigned ordinal = 0;
        for (int index = 0; index <= resultFailureIndex_; ++index)
          if (families_[index].failureReason) ++ordinal;
        std::snprintf(summary, sizeof(summary), tr(STR_FONT_BATCH_FAILED), ordinal, batchFailureCount_);
        renderer.drawCenteredText(UI_10_FONT_ID, centerY - lineHeight, summary);
        const int width = pageWidth - metrics.contentSidePadding * 2;
        const auto name = renderer.truncatedText(UI_10_FONT_ID, str(failed.name), width);
        renderer.drawCenteredText(UI_10_FONT_ID, centerY, name.c_str());
        if (failed.requiredMb) std::snprintf(summary, sizeof(summary), failed.failureReason, failed.requiredMb);
        else std::snprintf(summary, sizeof(summary), "%s", failed.failureReason);
        const auto reason = renderer.truncatedText(UI_10_FONT_ID, summary, width);
        renderer.drawCenteredText(UI_10_FONT_ID, centerY + lineHeight + metrics.verticalSpacing, reason.c_str());
      }
    } else {
      renderer.drawCenteredText(UI_10_FONT_ID, centerY, tr(STR_FONT_INSTALLED), true, EpdFontFamily::BOLD);
    }
    const bool pages = batchResult_ && batchFailureCount_ > 1;
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", pages ? tr(STR_DIR_UP) : "", pages ? tr(STR_DIR_DOWN) : "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state_ == ERROR) {
    renderer.drawCenteredText(UI_10_FONT_ID, centerY - lineHeight, tr(STR_FONT_INSTALL_FAILED), true,
                              EpdFontFamily::BOLD);
    if (!errorMessage_.empty()) {
      renderer.drawCenteredText(UI_10_FONT_ID, centerY + metrics.verticalSpacing, errorMessage_.c_str());
    }
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_RETRY), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  renderer.displayBuffer();
}

std::string FontDownloadActivity::x3InfoText() const {
  if (state_ == LOADING_MANIFEST) return tr(STR_LOADING_FONT_LIST);
  if (state_ == ERROR) return std::string(tr(STR_FONT_INSTALL_FAILED)) + "\n" + errorMessage_;
  char summary[96];
  if (state_ == COMPLETE) {
    if (!batchResult_) return tr(STR_FONT_INSTALLED);
    std::snprintf(summary, sizeof(summary), tr(STR_FONT_BATCH_DONE), batchSuccessCount_, batchFamilyCount_);
    std::string text = summary;
    if (batchFailureCount_ && resultFailureIndex_ < static_cast<int>(families_.size())) {
      const auto& failed = families_[resultFailureIndex_];
      unsigned ordinal = 0;
      for (int index = 0; index <= resultFailureIndex_; ++index)
        if (families_[index].failureReason) ++ordinal;
      std::snprintf(summary, sizeof(summary), tr(STR_FONT_BATCH_FAILED), ordinal, batchFailureCount_);
      text += std::string("\n") + summary + "\n" + str(failed.name);
      if (failed.requiredMb) std::snprintf(summary, sizeof(summary), failed.failureReason, failed.requiredMb);
      else std::snprintf(summary, sizeof(summary), "%s", failed.failureReason);
      text += std::string("\n") + summary;
    }
    return text;
  }
  const auto& family = families_[downloadingFamilyIndex_];
  const auto familyBytes = uint64_t{familyDownloadedBytes_} + fileProgress_;
  const auto totalBytes = batchRunning_ ? batchTotalBytes_ : family.totalSize;
  const auto downloadedBytes = batchRunning_ ? batchDownloadedBytes_ + fileProgress_ : familyBytes;
  const unsigned totalPercent = totalBytes ? static_cast<unsigned>(downloadedBytes * 100 / totalBytes) : 0;
  std::string text = str(family.name);
  if (installingPack_) text += std::string("\n") + tr(STR_FONT_PACK_INSTALLING);
  std::snprintf(summary, sizeof(summary), tr(STR_FONT_BATCH_DOWNLOADING),
                formatSize(familyBytes).c_str(), formatSize(family.totalSize).c_str());
  text += std::string("\n") + summary;
  std::snprintf(summary, sizeof(summary), tr(STR_FONT_BATCH_TOTAL), batchRunning_ ? batchFamilyIndex_ : 1u,
                batchRunning_ ? batchFamilyCount_ : 1u, totalPercent);
  text += std::string("\n") + summary + "\n" + formatSize(downloadedBytes) + " / " + formatSize(totalBytes);
  return text;
}

void FontDownloadActivity::renderX3State() {
  renderer.clearScreen();
  const auto layout = tenorchrome::x3ScreenLayout(renderer);
  auto body = layout.bodyRect;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  body.height -= lineHeight + 8;
  if (state_ == ERROR) body.height -= lineHeight + 8;
  if (state_ == DOWNLOADING && fileTotal_ > 0) body.height -= metrics.progressBarHeight + 8;
  const std::string text = x3InfoText();
  infoPages_ = x3settings::infoPageCount(renderer, text.c_str(), body);
  infoPage_ = std::clamp(infoPage_, 0, infoPages_ - 1);
  x3settings::drawInfoPage(renderer, text.c_str(), body, infoPage_);
  if (state_ == DOWNLOADING && fileTotal_ > 0) {
    const auto& family = families_[downloadingFamilyIndex_];
    const auto bytes = uint64_t{familyDownloadedBytes_} + fileProgress_;
    const unsigned percent = family.totalSize ? static_cast<unsigned>(bytes * 100 / family.totalSize) : 0;
    GUI.drawProgressBar(renderer, Rect{metrics.contentSidePadding, body.bottom() + 8,
                                       renderer.getScreenWidth() - 2 * metrics.contentSidePadding, metrics.progressBarHeight},
                         std::min(percent, 100u), 100);
  }
  auto row = layout.bodyRect;
  row.y = row.bottom() - lineHeight;
  row.height = lineHeight;
  if (state_ == ERROR) {
    tenorchrome::drawInfoInRect(renderer, tr(STR_RETRY), row, 1);
    row.y -= lineHeight + 8;
  }
  if (infoPages_ > 1) {
    const std::string position = std::to_string(infoPage_ + 1) + "/" + std::to_string(infoPages_);
    tenorchrome::drawInfoInRect(renderer, position.c_str(), row, 1);
  }
  drawChrome();
  const bool pages = infoPages_ > 1 || (state_ == COMPLETE && batchResult_ && batchFailureCount_ > 1);
  const auto labels = mappedInput.mapLabels(state_ == LOADING_MANIFEST ? ""
                                             : state_ == DOWNLOADING ? (installingPack_ ? "" : tr(STR_CANCEL)) : tr(STR_BACK),
                                             state_ == ERROR ? tr(STR_SELECT) : "",
                                             pages ? tr(STR_DIR_UP) : "", pages ? tr(STR_DIR_DOWN) : "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
