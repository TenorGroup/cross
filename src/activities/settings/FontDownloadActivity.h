#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "FontManifestValidation.h"
#include "FontInstaller.h"
#include "SdCardFont.h"
#include "activities/UiListActivity.h"
#include "activities/settings/FontDownloadProgress.h"

// JSON schema version of the fonts.json manifest. The canonical version for
// the build tooling lives in lib/EpdFont/scripts/cpfont_version.py. This
// firmware-side copy must be bumped manually when the firmware is updated to
// support a new manifest schema.
#define FONTS_MANIFEST_VERSION 1

namespace font_power {
enum class Phase { Idle, LoadingManifest, Downloading, Complete, Error };

inline bool preventsAutoSleep(const Phase phase) {
  return phase == Phase::LoadingManifest || phase == Phase::Downloading;
}
}  // namespace font_power

#ifndef FONT_MANIFEST_URL
// Pin the compatible four-weight font pack independently of future app versions.
// Publication stages this immutable directory before making the firmware available.
#define FONT_MANIFEST_URL "https://cross.tenor.vn/firmware/v1.0.8/fonts/fonts.json"
#endif

class FontDownloadActivity final : public UiListActivity {
 public:
  explicit FontDownloadActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

#ifdef FREEINK_TLS_AUDIT
  void setAuditDownload() { auditDownload_ = true; }
#endif
  void onEnter() override;
  void onExit() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override {
    font_power::Phase phase = font_power::Phase::Idle;
    if (state_ == LOADING_MANIFEST) phase = font_power::Phase::LoadingManifest;
    if (state_ == DOWNLOADING) phase = font_power::Phase::Downloading;
    if (state_ == COMPLETE) phase = font_power::Phase::Complete;
    if (state_ == ERROR) phase = font_power::Phase::Error;
    return font_power::preventsAutoSleep(phase);
  }
  bool skipLoopDelay() override { return true; }

 private:
#ifdef FREEINK_TLS_AUDIT
  bool auditDownload_ = false;
#endif
  enum State {
    WIFI_SELECTION,
    LOADING_MANIFEST,
    GROUP_LIST,
    FAMILY_LIST,
    DOWNLOADING,
    COMPLETE,
    ERROR,
  };

  // Byte offset into stringArena_; 0 is the empty string.
  using StrRef = uint32_t;

  struct ManifestFile {
    StrRef name = 0;
    uint32_t size = 0;
    uint32_t crc32 = 0;
  };

  struct ManifestFamily {
    StrRef name = 0;
    StrRef description = 0;
    // Range into files_, which holds every family's files back to back.
    uint32_t fileStart = 0;
    uint32_t fileCount = 0;
    uint32_t totalSize = 0;
    uint32_t scriptMask = 0;
    bool installed = false;
    bool hasUpdate = false;
  };

  static constexpr size_t MAX_SCRIPT_GROUPS = 32;

  State state_ = WIFI_SELECTION;
  FontInstaller fontInstaller_;

  // Manifest data
  std::string baseUrl_;
  // Reused for every file of every family: downloadToFile takes a std::string,
  // so a char buffer would just build a temporary per call.
  std::string downloadUrl_;
  // Manifest strings, null-terminated and packed back to back.
  std::unique_ptr<char[]> stringArena_;
  uint32_t arenaUsed_ = 0;
  uint32_t arenaCapacity_ = 0;
  std::vector<ManifestFamily> families_;
  // Every family's files back to back; sized once from the manifest, so it is
  // allocated nothrow like the arena rather than through vector::reserve.
  std::unique_ptr<ManifestFile[]> files_;
  uint32_t fileEntryCount_ = 0;
  // Manifest-defined labels are dynamic; cap them at the 32-bit membership
  // mask and retain only labels after parsing so group tags consume no steady-state heap.
  std::vector<StrRef> scriptGroupLabels_;
  // One 4-byte index per manifest family, allocated once and reused for every group.
  std::vector<int> filteredIndices_;
  freeink::ui::ListNav groupNav_;

  // Download progress
  size_t currentFileIndex_ = 0;
  size_t currentFileTotal_ = 0;
  size_t fileProgress_ = 0;
  size_t fileTotal_ = 0;
  // This is read and updated only under RenderLock. It lets the downloader
  // keep polling Back while an e-ink frame holds the renderer mutex.
  fontdownload::ProgressRenderGate progressRenderGate_;
  int downloadingFamilyIndex_ = 0;
  std::string errorMessage_;
  bool cancelRequested_ = false;
  bool runtimeStarted_ = false;
  static constexpr unsigned long TERMINAL_IDLE_TIMEOUT_MS = 5UL * 60UL * 1000UL;
  unsigned long terminalStateSince_ = 0;
  bool terminalIdleTimerStarted_ = false;
  // Set when the cancel came from the home gesture (consumed by the download
  // callback's own input pump); exit to home after the abort unwinds.
  bool goHomeRequested_ = false;

  // Shared cache for group and family rows. It is rebuilt only when the visible
  // list changes, never for cursor movement or tap flash repaints.
  std::vector<std::string> rowLabels_;
  std::vector<freeink::ui::ListItem> rowItems_;
  bool rowsDirty_ = true;
  void rebuildRowItems();
  void rebuildGroupRowItems();
  void rebuildFamilyRowItems();

  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  freeink::ui::ListNav& activeNav() override;
  void onBackButton() override;
  // Non-list states (loading, downloading, complete, error) consume the loop
  // pass here; the group and family lists use the base list protocol.
  bool handleCustomInput() override;
  bool terminalStateIdleExpired(unsigned long now) const;

  void activateSelected();

  void onWifiSelectionComplete(bool success);
  bool fetchAndParseManifest();
  // cppcheck-suppress arithOperationsOnVoidPointer // unique_ptr<char[]>::get() is char*, not void*
  const char* str(StrRef ref) const { return stringArena_ ? stringArena_.get() + ref : ""; }
  // Returns false if the string does not fit the arena reserved for the manifest.
  bool internString(const char* text, StrRef& outRef);
  void clearManifest();
  void downloadFamily(ManifestFamily& family);
  void downloadAll();
  void updateAll();
  static bool computeFileCrc32(const char* path, uint32_t& outCrc);
  bool showDownloadAllRow() const;
  bool showUpdateAllRow() const;
  int specialRowCount() const;
  bool isDownloadAllRow(int index) const;
  bool isUpdateAllRow(int index) const;
  bool isSelectedFamilyDeletable() const;
  void promptDeleteSelectedFamily();
  void onDeleteConfirmationResult(const ActivityResult& result);
  int familyIndexFromList(int listIndex) const;
  int listItemCount() const;
  bool hasGroupScreen() const { return !scriptGroupLabels_.empty(); }
  int groupListItemCount() const { return 1 + static_cast<int>(scriptGroupLabels_.size()); }
  int groupMemberCount(int scriptGroupIndex) const;
  void buildFilteredIndices(int groupListIndex);
  void enterGroup(int groupListIndex);
  size_t totalDownloadSize() const;
  size_t totalUpdateSize() const;
  static std::string formatSize(size_t bytes);
};
