#pragma once
#include "activities/X3ActivityLayout.h"

#include <string>
#include <utility>

#include "activities/Activity.h"
#if TENOR_CABLE_FLASH
#include "platform/CableFlash.h"
#endif

/**
 * SD-card based firmware update activity.
 *
 * Flow:
 *  1) onEnter -> push FileBrowserActivity in PickFirmware mode (only .bin files visible).
 *  2) On result: validate the .bin (header magic, size fits OTA partition).
 *  3) Push ConfirmationActivity ("Update firmware?").
 *  4) On confirm: stream the file into the OTA partition via the Arduino Update API,
 *     drawing a progress bar; on success ESP.restart().
 *
 * Used both from Settings -> System -> "SD Card Firmware Update", and as the only
 * activity launched in boot recovery mode (left side button + power on X3).
 */
class SdFirmwareUpdateActivity : public Activity {
  x3activity::InfoPages x3Info;

 public:
  tenorchrome::X3PathIcon navigationIcon() const override { return tenorchrome::X3PathIcon::System; }
  std::string x3NavigationLabel() const override { return I18N.get(title()); }
  enum class State {
    PICKING,
    VALIDATING,
    CONFIRMING,
    UPDATING,
    SUCCESS,
    FAILED,
  };

  explicit SdFirmwareUpdateActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool recoveryMode = false)
      : Activity("SdFirmwareUpdate", renderer, mappedInput), recoveryMode(recoveryMode) {}
#if TENOR_CABLE_FLASH
  // The image comes over the cable (platform/CableFlashLink.h) instead of from the card. The user is
  // asked first, unless the recovery keys were held at power on (`consented`).
  SdFirmwareUpdateActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const cableflash::Begin& begin,
                           bool consented, std::string returnBook = {})
      : Activity("SdFirmwareUpdate", renderer, mappedInput), cable(true), cableStart(consented), cableBegin(begin),
        cableReturnBook(std::move(returnBook)) {}
  static const SdFirmwareUpdateActivity* activeUpdate();
  bool isCable() const { return cable; }
  bool cableFailed() const { return cable && state == State::FAILED; }
  const std::string& cableBookToReturn() const { return cableReturnBook; }
#endif

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state == State::UPDATING || state == State::VALIDATING; }
  bool skipLoopDelay() override { return state == State::UPDATING; }

 private:
  State state = State::PICKING;
  bool recoveryMode = false;
  bool runtimeStarted = false;

  std::string firmwarePath;
  size_t firmwareSize = 0;
  size_t writtenBytes = 0;
  unsigned int lastRenderedPercent = 101;
  std::string errorMessage;

  void launchPicker();
  void onPickerResult(const ActivityResult& result);
  bool validateFirmware();
  void promptConfirmation();
  void onConfirmationResult(const ActivityResult& result);
  void performUpdate();
  StrId title() const;
#if TENOR_CABLE_FLASH
  bool cable = false;
  bool cableStart = false;  // the session starts on the next loop pass
  cableflash::Begin cableBegin;
  std::string cableReturnBook;
  void performCableUpdate();
  void leaveCableSession();
#else
  static constexpr bool cable = false;
#endif
};
