#include "SdFirmwareUpdateActivity.h"

#include "activities/X3ActivityLayout.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <esp_ota_ops.h>

#include "FileTransferState.h"
#include "MappedInputManager.h"
#include "activities/home/FileBrowserActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "shells/Shell.h"
#include "shells/ugly/UglyInk.h"
#include "shells/ugly/UglyWords.h"
#include "network/FirmwareFlasher.h"
#if TENOR_CABLE_FLASH
#include "platform/CableFlashLink.h"

namespace {
const SdFirmwareUpdateActivity* activeUpdateActivity = nullptr;
}

const SdFirmwareUpdateActivity* SdFirmwareUpdateActivity::activeUpdate() { return activeUpdateActivity; }
#endif

void SdFirmwareUpdateActivity::onEnter() {
  runtimeStarted = false;
  // Stop BLE before entering the picker and before any validation/flash path.
  if (!filetransfer::acquire()) {
    LOG_ERR("FW", "BLE teardown incomplete; leaving SD firmware update");
#if TENOR_CABLE_FLASH
    if (cable) {
      cableflash::say("TCF1 FAIL busy");
      leaveCableSession();
      return;
    }
#endif
    finish();
    return;
  }
  runtimeStarted = true;
  Activity::onEnter();
#if TENOR_CABLE_FLASH
  activeUpdateActivity = this;
#endif
  // Build-identity marker - confirms which firmware build owns the SD update flow.
  LOG_INF("FW", "SdFirmwareUpdateActivity build=%s %s recovery=%d", __DATE__, __TIME__, recoveryMode ? 1 : 0);
  state = State::PICKING;
#if TENOR_CABLE_FLASH
  if (cable) {
    if (!cableStart) promptConfirmation();
    return;
  }
#endif
  launchPicker();
}

StrId SdFirmwareUpdateActivity::title() const {
#if TENOR_CABLE_FLASH
  if (cable) return StrId::STR_CABLE_UPDATE;
#endif
  return recoveryMode ? StrId::STR_RECOVERY_MODE : StrId::STR_SD_FIRMWARE_UPDATE;
}

void SdFirmwareUpdateActivity::onExit() {
#if TENOR_CABLE_FLASH
  if (activeUpdateActivity == this) activeUpdateActivity = nullptr;
#endif
  Activity::onExit();
  // The owner covers picker, validation, confirmation, and the synchronous
  // flash. Release it only once this activity is actually leaving.
  filetransfer::release();
}

#if TENOR_CABLE_FLASH
void SdFirmwareUpdateActivity::leaveCableSession() {
  const auto decision = cableflash::decidePolicy(
      cableflash::PolicyInput{true, false, false, false, true, cableFailed(), cableReturnBook.c_str()});
  if (decision.action == cableflash::PolicyAction::ReturnToBook) {
    std::string book = cableReturnBook;
    activityManager.goToReader(std::move(book));
    return;
  }
  finish();
}
#endif

void SdFirmwareUpdateActivity::launchPicker() {
  // Reuse the standard file browser, restricted to .bin files only.
  startActivityForResult(
      std::make_unique<FileBrowserActivity>(renderer, mappedInput, "/", FileBrowserActivity::Mode::PickFirmware),
      [this](const ActivityResult& result) { onPickerResult(result); });
}

void SdFirmwareUpdateActivity::onPickerResult(const ActivityResult& result) {
  if (result.isCancelled) {
    if (recoveryMode) {
      // Recovery mode: re-launch the picker so the user cannot escape into a half-initialised UI.
      launchPicker();
      return;
    }
    finish();
    return;
  }

  const auto* path = std::get_if<FilePathResult>(&result.data);
  if (!path) {
    LOG_ERR("FW", "Picker returned no path");
    finish();
    return;
  }
  firmwarePath = path->path;
  LOG_DBG("FW", "Selected: %s", firmwarePath.c_str());

  {
    RenderLock lock(*this);
    state = State::VALIDATING;
  }
  requestUpdateAndWait();

  if (!validateFirmware()) {
    RenderLock lock(*this);
    state = State::FAILED;
    requestUpdate();
    return;
  }

  promptConfirmation();
}

bool SdFirmwareUpdateActivity::validateFirmware() {
  HalFile file;
  if (!Storage.openFileForRead("FW", firmwarePath.c_str(), file) || !file) {
    errorMessage = tr(STR_FIRMWARE_FILE_OPEN_FAILED);
    return false;
  }
  firmwareSize = file.fileSize();
  file.close();

  // Resolve the next-update partition directly via the OTA API. Previously this
  // probed via Update.begin(firmwareSize)/Update.abort() to learn the partition
  // size, which had the side effect of erasing partition state and was wasted
  // work since we only need the size bound for validation here.
  const esp_partition_t* dest = esp_ota_get_next_update_partition(nullptr);
  if (!dest) {
    LOG_ERR("FW", "no next-update partition available");
    errorMessage = tr(STR_INVALID_FIRMWARE);
    return false;
  }
  const size_t partitionLimit = dest->size;
  if (firmwareSize > partitionLimit) {
    LOG_ERR("FW", "firmware (%u bytes) exceeds partition (%u bytes)", static_cast<unsigned>(firmwareSize),
            static_cast<unsigned>(partitionLimit));
    errorMessage = tr(STR_FIRMWARE_TOO_LARGE);
    return false;
  }

  // Run the same end-to-end integrity check (header / segment table / XOR checksum / SHA256
  // trailer) that the shared firmware-flasher applies right before raw-writing otadata. This
  // catches truncated or corrupted .bin files at confirmation time, before the user ever sees
  // the "Updating…" progress bar.
  const auto vr = firmware_flash::validateImageFile(firmwarePath.c_str(), partitionLimit);
  if (vr != firmware_flash::Result::OK) {
    LOG_ERR("FW", "image validation failed: %s", firmware_flash::resultName(vr));
    if (vr == firmware_flash::Result::TOO_LARGE) {
      errorMessage = tr(STR_FIRMWARE_TOO_LARGE);
    } else if (vr == firmware_flash::Result::TOO_SMALL) {
      errorMessage = tr(STR_FIRMWARE_TOO_SMALL);
    } else if (vr == firmware_flash::Result::BAD_CHIP || vr == firmware_flash::Result::WRONG_BOARD) {
      errorMessage = tr(STR_FIRMWARE_WRONG_DEVICE);
    } else {
      errorMessage = tr(STR_INVALID_FIRMWARE);
    }
    return false;
  }
  return true;
}

void SdFirmwareUpdateActivity::promptConfirmation() {
  {
    RenderLock lock(*this);
    state = State::CONFIRMING;
  }
  // Show "Update firmware?" with the file path as the body line; over the cable, the version the
  // computer offers (the session checks the image says the same).
  std::string heading = cable ? tr(STR_CABLE_UPDATE_PROMPT) : tr(STR_FIRMWARE_UPDATE_PROMPT);
  // Use the basename only to keep the body short.
  std::string body = firmwarePath;
  const auto pos = body.find_last_of('/');
  if (pos != std::string::npos) body = body.substr(pos + 1);
#if TENOR_CABLE_FLASH
  if (cable) body = cableBegin.version;
#endif

  auto content = ConfirmationContent{heading, body, tr(STR_ALERT_FIRMWARE_ACTION_SHORT),
                                     tr(STR_ALERT_FIRMWARE_ACTION_LONG), "", false};
#if TENOR_CABLE_FLASH
  if (cable) {
    content.timeoutMs = cableflash::kConsentMs;
    content.touchMessage = heading + " " + body;  // the touch sheet shows one message: say who asks
  }
#endif
  startActivityForResult(
      std::make_unique<ConfirmationActivity>(renderer, mappedInput, heading, body, std::move(content)),
      [this](const ActivityResult& result) { onConfirmationResult(result); });
}

void SdFirmwareUpdateActivity::onConfirmationResult(const ActivityResult& result) {
#if TENOR_CABLE_FLASH
  if (cable && result.isCancelled) {
    cableflash::say("TCF1 FAIL denied");  // No, or nobody answered within the minute
    leaveCableSession();
    return;
  }
  if (cable) {
    {
      RenderLock lock(*this);
      state = State::UPDATING;
      writtenBytes = 0;
      firmwareSize = cableBegin.size;
      lastRenderedPercent = 101;
    }
    requestUpdateAndWait();
    performCableUpdate();
    return;
  }
#endif
  if (result.isCancelled) {
    if (recoveryMode) {
      // Go back to the picker rather than exiting recovery.
      launchPicker();
      return;
    }
    finish();
    return;
  }

  {
    RenderLock lock(*this);
    state = State::UPDATING;
    writtenBytes = 0;
    lastRenderedPercent = 101;
  }
  requestUpdateAndWait();
  performUpdate();
}

void SdFirmwareUpdateActivity::performUpdate() {
  LOG_INF("FW", "SD update: %s (%u bytes)", firmwarePath.c_str(), static_cast<unsigned>(firmwareSize));

  auto progressCb = +[](size_t written, size_t total, void* ctx) {
    auto* self = static_cast<SdFirmwareUpdateActivity*>(ctx);
    self->writtenBytes = written;
    self->firmwareSize = total;
    // immediate=true: wake the render task directly. We're in a tight sync
    // loop so the main loop won't drain the requestedUpdate flag for us.
    self->requestUpdate(true);
  };

  // Re-validate at flash time (TOCTOU): SD is removable, so don't trust the
  // pre-confirmation pass. The alreadyValidated parameter on the API stays
  // for callers (e.g. an OTA staging path) where the same byte stream was
  // just hashed and there's no removable-media gap.
  const auto result = firmware_flash::flashFromSdPath(firmwarePath.c_str(), progressCb, this);
  if (result != firmware_flash::Result::OK) {
    LOG_ERR("FW", "flash failed: %s", firmware_flash::resultName(result));
    // BAD_CHIP / WRONG_BOARD here is the TOCTOU re-validation catching a
    // wrong-device image the pre-confirmation pass missed (e.g. the SD card
    // was swapped).
    errorMessage = result == firmware_flash::Result::BAD_CHIP || result == firmware_flash::Result::WRONG_BOARD
                       ? tr(STR_FIRMWARE_WRONG_DEVICE)
                                                                : tr(STR_FIRMWARE_WRITE_FAILED);
    RenderLock lock(*this);
    state = State::FAILED;
    requestUpdate();
    return;
  }

  LOG_INF("FW", "SD firmware update complete, restarting");
  {
    RenderLock lock(*this);
    state = State::SUCCESS;
  }
  requestUpdateAndWait();
  delay(1500);
  ESP.restart();
}

#if TENOR_CABLE_FLASH && defined(SIMULATOR)
namespace {
// The simulator has no cable. CROSSPOINT_SIM_CABLE="<percent>:<result>" plays a session that reaches
// <percent> and ends with <result> (a cableflash::reason name), or stays there with "hold".
bool simulatorCableResult(void (*progress)(size_t, size_t, void*), void* ctx, size_t total,
                          cableflash::Result& result) {
  const char* spec = std::getenv("CROSSPOINT_SIM_CABLE");
  unsigned percent = 0;
  char name[24] = "silence";
  if (spec) std::sscanf(spec, "%u:%23s", &percent, name);
  progress(total * percent / 100, total, ctx);
  result = cableflash::Result::Silence;
  for (int r = 0; r <= static_cast<int>(cableflash::Result::BadSha); ++r) {
    if (std::strcmp(cableflash::reason(static_cast<cableflash::Result>(r)), name) == 0)
      result = static_cast<cableflash::Result>(r);
  }
  return std::strcmp(name, "hold") != 0;
}
}  // namespace
#endif

#if TENOR_CABLE_FLASH
void SdFirmwareUpdateActivity::performCableUpdate() {
  LOG_INF("FW", "Cable update: %s (%lu bytes)", cableBegin.version, static_cast<unsigned long>(cableBegin.size));
  auto progressCb = +[](size_t written, size_t total, void* ctx) {
    auto* self = static_cast<SdFirmwareUpdateActivity*>(ctx);
    const bool newPercent = written * 100 / total != self->writtenBytes * 100 / total;
    self->writtenBytes = written;
    self->firmwareSize = total;
    // Only a new percent wakes the render task: the frames come 1 KB at a time.
    if (newPercent) self->requestUpdate(true);
  };
#ifdef SIMULATOR
  cableflash::Result result;
  if (!simulatorCableResult(progressCb, this, cableBegin.size, result)) return;
#else
  const auto result = cableflash::run(cableBegin, progressCb, this);
#endif
  using R = cableflash::Result;
  if (result != R::Ok) {
    LOG_ERR("FW", "cable update failed: %s", cableflash::reason(result));
    errorMessage = result == R::WrongChip || result == R::WrongBoard                ? tr(STR_FIRMWARE_WRONG_DEVICE)
                   : result == R::NoCable                                            ? tr(STR_FIRMWARE_NO_CABLE)
                   : result == R::TooLarge                                           ? tr(STR_FIRMWARE_TOO_LARGE)
                   : result == R::Silence || result == R::Aborted || result == R::Noise ? tr(STR_CABLE_LOST)
                   : result == R::FlashFailed                                        ? tr(STR_FIRMWARE_WRITE_FAILED)
                                                                                     : tr(STR_INVALID_FIRMWARE);
    RenderLock lock(*this);
    state = State::FAILED;
    requestUpdate();
    return;
  }
  LOG_INF("FW", "Cable update complete, restarting");
  {
    RenderLock lock(*this);
    state = State::SUCCESS;
  }
  requestUpdateAndWait();
  delay(1500);
  ESP.restart();
}
#endif

void SdFirmwareUpdateActivity::loop() {
#if TENOR_CABLE_FLASH
  if (cableStart) {
    // The recovery keys were the yes: straight to the session.
    cableStart = false;
    onConfirmationResult(ActivityResult{});
    return;
  }
#endif
  if (tenorchrome::x3ScreenLayoutEnabled() &&
      x3Info.turn(mappedInput.wasReleased(MappedInputManager::Button::Up) ? -1 :
                  mappedInput.wasReleased(MappedInputManager::Button::Down) ? 1 : 0)) {
    requestUpdate();
    return;
  }

  if (state == State::FAILED) {
    int x = 0;
    int y = 0;
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasPressed(MappedInputManager::Button::Confirm) || mappedInput.wasScreenTapped(x, y)) {
      if (recoveryMode) {
        // Go back to picker so user can try a different .bin
        state = State::PICKING;
        launchPicker();
        return;
      }
#if TENOR_CABLE_FLASH
      if (cable) {
        leaveCableSession();
        return;
      }
#endif
      finish();
    }
  }
}

namespace {
// tenor/ugly on the button readers writes the lines of this screen by hand, centred; S22 when S30 is too wide.
void sayCentred(const GfxRenderer& r, const int baseline, const char* text, const bool straight = false) {
  const ugly::Size size =
      ugly::width(r, ugly::Size::S30, text, straight) <= r.getScreenWidth() - 24 ? ugly::Size::S30 : ugly::Size::S22;
  ugly::text(r, size, (r.getScreenWidth() - ugly::width(r, size, text, straight)) / 2, baseline, text, true, straight);
}
}  // namespace

void SdFirmwareUpdateActivity::render(RenderLock&&) {
  if (tenorchrome::x3ScreenLayoutEnabled()) {
    const char* status = state == State::VALIDATING ? tr(STR_VALIDATING_FIRMWARE) :
        state == State::UPDATING ? tr(STR_UPDATING) : state == State::SUCCESS ? tr(STR_UPDATE_COMPLETE) :
        state == State::FAILED ? tr(STR_UPDATE_FAILED) : recoveryMode ? tr(STR_RECOVERY_MODE_HINT) : nullptr;
    const char* detail = state == State::UPDATING ? tr(STR_FIRMWARE_UPDATE_DO_NOT_POWER_OFF) :
        state == State::SUCCESS ? tr(STR_RESTARTING_HINT) : nullptr;
    char progress[48] = {};
    if (state == State::UPDATING) {
      const unsigned int percent = firmwareSize ? static_cast<unsigned int>(writtenBytes * 100 / firmwareSize) : 0;
      if (percent == lastRenderedPercent) return;
      lastRenderedPercent = percent;
      snprintf(progress, sizeof(progress), "%u%%, %lu / %lu B", percent,
          static_cast<unsigned long>(writtenBytes), static_cast<unsigned long>(firmwareSize));
    }
    const char* tip = nullptr;
    const auto layout = tenorchrome::x3ScreenLayout(renderer, tenorchrome::tipHeight(renderer, tip, 8));
    renderer.clearScreen();
    x3Info.draw(renderer, layout.bodyRect, {status, detail, errorMessage.c_str(), progress});
    tenorchrome::drawTipInRect(renderer, tip, layout.tipRect, 8);
    x3activity::drawBar(renderer, layout.barRect, recoveryMode ? tr(STR_RECOVERY_MODE) : tr(STR_SD_FIRMWARE_UPDATE), navigationPrefix.c_str());
    const auto labels = mappedInput.mapLabels(state == State::FAILED ? tr(STR_BACK) : "", "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

    renderer.displayBuffer();
    return;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  const char* headerText = I18N.get(title());
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, headerText);

  const auto lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const auto top = (pageHeight - lineHeight) / 2;

  const bool handwritten = shell::uglyParts();
  const int baseline = top + ugly::ascent(ugly::Size::S30);
  if (state == State::VALIDATING) {
    if (handwritten) {
      sayCentred(renderer, baseline, tr(STR_VALIDATING_FIRMWARE));
    } else {
      renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_VALIDATING_FIRMWARE));
    }
  } else if (state == State::UPDATING) {
    // Throttle redraws to once per percent.
    const unsigned int pct = firmwareSize > 0 ? static_cast<unsigned int>((writtenBytes * 100) / firmwareSize) : 0;
    if (pct == lastRenderedPercent) {
      return;
    }
    lastRenderedPercent = pct;

    if (handwritten) {
      sayCentred(renderer, baseline, tr(STR_UPDATING));
    } else {
      renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_UPDATING), true, EpdFontFamily::BOLD);
    }

    int y = top + lineHeight + metrics.verticalSpacing;
    GUI.drawProgressBar(
        renderer,
        Rect{metrics.contentSidePadding, y, pageWidth - metrics.contentSidePadding * 2, metrics.progressBarHeight},
        static_cast<int>(pct), 100);
    y += metrics.progressBarHeight + metrics.verticalSpacing;
    // Percent label is drawn by BaseTheme::drawProgressBar; this slot is left intentionally empty
    // so the do-not-power-off line below stays at the same Y as before.
    y += lineHeight + metrics.verticalSpacing;
    if (handwritten) {
      // The line that says not to cut the power is read right.
      ugly::paragraph(renderer, ugly::Size::S30, metrics.contentSidePadding, y + ugly::ascent(ugly::Size::S30),
                      pageWidth - metrics.contentSidePadding * 2, 40, ugly::words::flashing(), true, ugly::STRAIGHT);
    } else {
      renderer.drawCenteredText(UI_10_FONT_ID, y, tr(STR_FIRMWARE_UPDATE_DO_NOT_POWER_OFF));
    }
  } else if (state == State::SUCCESS) {
    if (handwritten) {
      sayCentred(renderer, baseline, tr(STR_UPDATE_COMPLETE));
    } else {
      renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_UPDATE_COMPLETE), true, EpdFontFamily::BOLD);
    }
    const int hintY = top + lineHeight + metrics.verticalSpacing;
    const Rect hintBounds{metrics.contentSidePadding, hintY, pageWidth - metrics.contentSidePadding * 2,
                          pageHeight - hintY};
    UITheme::drawCenteredWrappedText(renderer, hintBounds, SMALL_FONT_ID, tr(STR_RESTARTING_HINT), 3, true,
                                     EpdFontFamily::REGULAR, UITheme::TextVerticalAlignment::TOP);
  } else if (state == State::FAILED) {
    if (handwritten) {
      sayCentred(renderer, baseline, tr(STR_UPDATE_FAILED));
      if (!errorMessage.empty()) {
        sayCentred(renderer, baseline + 44, errorMessage.c_str(), ugly::STRAIGHT);  // why it failed, read right
      }
    } else {
      renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_UPDATE_FAILED), true, EpdFontFamily::BOLD);
      if (!errorMessage.empty()) {
        renderer.drawCenteredText(UI_10_FONT_ID, top + lineHeight + metrics.verticalSpacing, errorMessage.c_str());
      }
    }
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else {
    // PICKING / CONFIRMING: a sub-activity is on top, nothing to draw.
    if (recoveryMode) {
      renderer.drawCenteredText(SMALL_FONT_ID, top, tr(STR_RECOVERY_MODE_HINT));
    }
  }

  renderer.displayBuffer();
}
