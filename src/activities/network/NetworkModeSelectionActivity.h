#pragma once

#include <BoardConfig.h>

#include "activities/UiListActivity.h"

// Keep USB_DRIVE at its historical ordinal. WebDAV and the font store are appended so persisted
// or test-provided mode values continue to select the same USB row.
enum class NetworkMode {
  JOIN_NETWORK,
  CONNECT_CALIBRE,
  CREATE_HOTSPOT,
  USB_DRIVE,
  WEB_DAV,
  FONT_STORE,
};

/**
 * NetworkModeSelectionActivity is the Connections hub (every way into File Transfer opens it):
 * - "Join a Network" - Connect to an existing WiFi network (STA mode)
 * - "Connect to Calibre" - Use Calibre wireless device transfers
 * - "Create Hotspot" - Create an Access Point that others can connect to (AP mode)
 * - USB drive and WebDAV where the board has them
 * - "Get fonts" - the font store over Wi-Fi, last on every board
 *
 * The onModeSelected callback is called with the user's choice.
 * The onCancel callback is called if the user presses back.
 *
 * The header stays on GUI.drawHeader for the battery indicator.
 */
class NetworkModeSelectionActivity final : public UiListActivity {
 public:
  tenorchrome::X3PathIcon navigationIcon() const override { return tenorchrome::X3PathIcon::Network; }
  void drawChrome() override;
  explicit NetworkModeSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

#if FREEINK_CAP_USB_MSC
  static constexpr int MENU_ITEM_COUNT = 6;
#elif !FREEINK_DEVICE_X3
  static constexpr int MENU_ITEM_COUNT = 5;
#else
  static constexpr int MENU_ITEM_COUNT = 4;
#endif

  void onModeSelected(NetworkMode mode);
  void onCancel();

 private:
  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override { onCancel(); }
  const char* headerTitle() const override;

  // Row storage: entirely static (label/subtitle/icon never change), so it's
  // built once in the constructor instead of every buildScreen() call, into
  // fixed-capacity storage that avoids any heap allocation for the row list.
  freeink::ui::ListItem rowItems_[MENU_ITEM_COUNT]{};
};
