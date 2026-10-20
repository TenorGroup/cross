#include "NetworkModeSelectionActivity.h"

#include "activities/X3ActivityLayout.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"

namespace fui = freeink::ui;

namespace {
// One row a mode, in screen order. The row's actionValue carries its mode: the rows a board has (USB, WebDAV)
// shift the indices, so an index is never a mode.
struct Row {
  NetworkMode mode;
  StrId label, description;
  UIIcon icon;
};
constexpr Row ROWS[NetworkModeSelectionActivity::MENU_ITEM_COUNT] = {
    {NetworkMode::JOIN_NETWORK, StrId::STR_JOIN_NETWORK, StrId::STR_JOIN_DESC, UIIcon::Wifi},
    {NetworkMode::CONNECT_CALIBRE, StrId::STR_CALIBRE_WIRELESS, StrId::STR_CALIBRE_DESC, UIIcon::Library},
    {NetworkMode::CREATE_HOTSPOT, StrId::STR_CREATE_HOTSPOT, StrId::STR_HOTSPOT_DESC, UIIcon::Hotspot},
#if FREEINK_CAP_USB_MSC
    {NetworkMode::USB_DRIVE, StrId::STR_USB_DRIVE, StrId::STR_USB_DRIVE_DESC, UIIcon::Usb},
#endif
#if !FREEINK_DEVICE_X3
    {NetworkMode::WEB_DAV, StrId::STR_WEBDAV_PULL, StrId::STR_WEBDAV_PULL_DESC, UIIcon::Wifi},
#endif
    {NetworkMode::FONT_STORE, StrId::STR_MANAGE_FONTS, StrId::STR_MANAGE_FONTS_SUB, UIIcon::Text},
};
}  // namespace

NetworkModeSelectionActivity::NetworkModeSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("NetworkModeSelection", renderer, mappedInput) {
  // Entirely static, so built once here rather than every buildScreen() call.
  for (int i = 0; i < MENU_ITEM_COUNT; i++) {
    fui::ListItem item;
    item.label = I18N.get(ROWS[i].label);
    item.subtitle = I18N.get(ROWS[i].description);
    item.icon = listIconFor(ROWS[i].icon, 32);  // subtitle rows carry the larger icon
    item.actionValue = static_cast<int16_t>(ROWS[i].mode);
    rowItems_[i] = item;
  }
}

int NetworkModeSelectionActivity::listCount() const { return MENU_ITEM_COUNT; }

const char* NetworkModeSelectionActivity::headerTitle() const { return tr(STR_FILE_TRANSFER); }

void NetworkModeSelectionActivity::activateIndex(const int index) {
  if (index < 0 || index >= MENU_ITEM_COUNT) return;
  // Selection leaves this screen; a lingering flash would gray an unrelated
  // element on the next render.
  app.clearTapFlash();
  nav.selected = index;

  onModeSelected(static_cast<NetworkMode>(rowItems_[index].actionValue));
}

void NetworkModeSelectionActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the GUI.drawHeader band, above the button hints.
  if (tenorchrome::x3ScreenLayoutEnabled()) {
    const auto layout = tenorchrome::x3ScreenLayout(renderer, tenorchrome::tipHeight(renderer, nullptr, 8));
    screen.setContentMarginFromScreen({static_cast<int16_t>(layout.bodyRect.y), 0,
        static_cast<int16_t>(renderer.getScreenHeight() - layout.bodyRect.bottom() + 0), 0});
  } else {
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  }
  if (!tenorchrome::x3ScreenLayoutEnabled()) screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  // rowItems_ was built once in the constructor and is reused here on every
  // repaint.
  fui::ListProps props;
  props.items = rowItems_;
  props.count = static_cast<uint16_t>(MENU_ITEM_COUNT);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.subtitleText = screen.theme().smallText;
  props.subtitleText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}

void NetworkModeSelectionActivity::onModeSelected(NetworkMode mode) {
  setResult(NetworkModeResult{mode});
  finish();
}

void NetworkModeSelectionActivity::onCancel() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

void NetworkModeSelectionActivity::drawChrome() {
  if (tenorchrome::x3ScreenLayoutEnabled()) {
    const auto layout = tenorchrome::x3ScreenLayout(renderer, tenorchrome::tipHeight(renderer, nullptr, 8));
    x3activity::drawBar(renderer, layout.barRect, headerTitle(), navigationPrefix.c_str());
    return;
  }
  UiListActivity::drawChrome();
}
