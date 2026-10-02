#include <cstdint>
#include <cstdio>
#include <cstdlib>

struct GfxRenderer {};

struct CrossPointSettings {
  uint8_t uiTheme = 0;
  uint8_t uiTextSize = 0;
  uint8_t other = 0;
} SETTINGS;

struct AtomicFlag {
  bool value = false;
  void store(bool next) { value = next; }
};

struct UITheme {
  static int reloads;
  static UITheme& getInstance() {
    static UITheme instance;
    return instance;
  }
  void reload() { ++reloads; }
};
int UITheme::reloads = 0;

struct SettingsActivity;
struct RenderLock {
  static int acquisitions;
  static bool held;
  explicit RenderLock(SettingsActivity&) { ++acquisitions; held = true; }
  ~RenderLock() { held = false; }
};
int RenderLock::acquisitions = 0;
bool RenderLock::held = false;

bool nextApplyResult = true;
int applyCalls = 0;
uint8_t appliedTier = 255;
GfxRenderer* appliedRenderer = nullptr;
uint8_t settingSeenDuringApply = 255;
bool lockSeenDuringApply = false;
bool applyUiFontSize(GfxRenderer& renderer, const uint8_t tier) {
  ++applyCalls;
  appliedRenderer = &renderer;
  appliedTier = tier;
  settingSeenDuringApply = SETTINGS.uiTextSize;
  lockSeenDuringApply = RenderLock::held;
  return nextApplyResult;
}

#define LOG_ERR(...) (++loggedErrors)
int loggedErrors = 0;

struct SettingsActivity {
  GfxRenderer renderer;
  AtomicFlag saveFailed;
  int resets = 0;
  void resetUi() { ++resets; }
  bool applyUiSettingChange(uint8_t CrossPointSettings::* valuePtr, uint8_t previousValue);
};

#include "ApplyUiSettingChange.inc"

namespace {
int failures = 0;
int scenarios = 0;

#define CHECK(condition)                                                                                               \
  do {                                                                                                                 \
    if (!(condition)) {                                                                                                \
      ++failures;                                                                                                      \
      std::printf("FAIL %s:%d: %s\n", __func__, __LINE__, #condition);                                                \
    }                                                                                                                  \
  } while (0)

void resetBoundaries(SettingsActivity& activity) {
  UITheme::reloads = 0;
  RenderLock::acquisitions = 0;
  nextApplyResult = true;
  applyCalls = 0;
  appliedTier = 255;
  appliedRenderer = nullptr;
  settingSeenDuringApply = 255;
  lockSeenDuringApply = false;
  loggedErrors = 0;
  activity.resets = 0;
  activity.saveFailed.value = false;
}

void sizeSuccessAppliesUnderLockAndInvalidates() {
  ++scenarios;
  SettingsActivity activity;
  resetBoundaries(activity);
  SETTINGS.uiTextSize = 0;
  CHECK(activity.applyUiSettingChange(&CrossPointSettings::uiTextSize, 2));
  CHECK(RenderLock::acquisitions == 1);
  CHECK(applyCalls == 1);
  CHECK(appliedRenderer == &activity.renderer);
  CHECK(appliedTier == 2);
  CHECK(settingSeenDuringApply == 0);
  CHECK(lockSeenDuringApply);
  CHECK(SETTINGS.uiTextSize == 2);
  CHECK(UITheme::reloads == 1);
  CHECK(activity.resets == 1);
  CHECK(!activity.saveFailed.value);
}

void sizeFailureSurfacesErrorWithoutReloadingLayout() {
  ++scenarios;
  SettingsActivity activity;
  resetBoundaries(activity);
  SETTINGS.uiTextSize = 2;
  nextApplyResult = false;
  CHECK(!activity.applyUiSettingChange(&CrossPointSettings::uiTextSize, 1));
  CHECK(RenderLock::acquisitions == 1);
  CHECK(applyCalls == 1);
  CHECK(settingSeenDuringApply == 2);
  CHECK(lockSeenDuringApply);
  CHECK(SETTINGS.uiTextSize == 2);
  CHECK(!activity.saveFailed.value);
  CHECK(loggedErrors == 1);
  CHECK(UITheme::reloads == 0);
  CHECK(activity.resets == 0);
}

void unrelatedSettingDoesNothing() {
  ++scenarios;
  SettingsActivity activity;
  resetBoundaries(activity);
  CHECK(activity.applyUiSettingChange(&CrossPointSettings::other, 0));
  CHECK(RenderLock::acquisitions == 0);
  CHECK(applyCalls == 0);
  CHECK(UITheme::reloads == 0);
  CHECK(activity.resets == 0);
}
}  // namespace

int main() {
  sizeSuccessAppliesUnderLockAndInvalidates();
  sizeFailureSurfacesErrorWithoutReloadingLayout();
  unrelatedSettingDoesNothing();
  std::printf("%d scenarios, %d failures\n", scenarios, failures);
  return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
