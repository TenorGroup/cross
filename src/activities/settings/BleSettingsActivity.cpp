#include "BleSettingsActivity.h"

#include <BlePageTurner.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <cstring>

#include "CrossPointSettings.h"
#include "I18nKeys.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace {
constexpr int16_t ROW_ENABLE = -1;
constexpr int16_t ROW_SCAN = -2;
constexpr int16_t ROW_NONE = -3;
constexpr int16_t ROW_FOUND = 0;   // + index of a device the scan found
constexpr int16_t ROW_BOND = 100;  // + index of a paired remote
constexpr uint32_t SCAN_MS = 15000;

void copyField(char* dest, const char* src, const size_t size) {
  strncpy(dest, src, size - 1);
  dest[size - 1] = '\0';
}
}  // namespace

void BleSettingsActivity::onEnter() {
  UiListActivity::onEnter();
  // A saved opt-in brings the radio up here, so scanning and pairing work at once.
  if (SETTINGS.ble.enabled) bleturner::switchOn();
  refresh();
  requestUpdate();
}

void BleSettingsActivity::loop() {
  UiListActivity::loop();
  if (millis() - lastPollMs < 250) return;
  lastPollMs = millis();
  if (SETTINGS.ble.enabled) bleturner::service();
  if (signature() == shownSignature) return;  // an e-ink repaint only when something changed
  refresh();
  requestUpdate();
}

const char* BleSettingsActivity::headerTitle() const { return tr(STR_BT_PAGE_TURNER); }

uint32_t BleSettingsActivity::signature() const {
  const auto st = bleturner::status();
  return (st.running ? 1u : 0u) | (st.scanning ? 2u : 0u) | (st.connected ? 4u : 0u) | (st.connecting ? 8u : 0u) |
         (st.stopping ? 16u : 0u) | (SETTINGS.ble.enabled ? 32u : 0u) |
         static_cast<uint32_t>(bleturner::bondCount()) << 8 | static_cast<uint32_t>(bleturner::foundCount()) << 16;
}

void BleSettingsActivity::refresh() {
  RenderLock lock(*this);
  shownSignature = signature();
  const auto st = bleturner::status();
  char failure[48];
  if (!SETTINGS.ble.enabled || st.stopping) {
    status = tr(STR_STATE_OFF);
  } else if (bleturner::takeConnectFailure(failure, sizeof(failure))) {
    status = failure;
  } else if (!st.running) {
    status = tr(STR_BT_START_FAILED);
  } else if (st.connected) {
    status = bleturner::linked().name;
  } else if (st.connecting) {
    status = tr(STR_CONNECTING);
  } else if (st.scanning) {
    status = tr(STR_SCANNING);
  } else {
    status = tr(STR_STATE_ON);
  }

  // Labels first: the rows point into them.
  const uint8_t found = st.running ? bleturner::foundCount() : 0;
  const uint8_t bonds = st.running ? bleturner::bondCount() : 0;
  labels.clear();
  for (uint8_t i = 0; i < found; ++i) labels.emplace_back(bleturner::found(i).name);
  for (uint8_t i = 0; i < bonds; ++i) {
    const auto peer = bleturner::bond(i);
    labels.emplace_back(peer.name[0] != '\0' ? peer.name : peer.addr);
  }

  rows.clear();
  fui::ListItem enable;
  enable.label = tr(STR_BT_PAGE_TURNER);
  enable.subtitle = status.c_str();
  enable.toggle = true;
  enable.toggleChecked = SETTINGS.ble.enabled != 0;
  enable.actionValue = ROW_ENABLE;
  rows.push_back(enable);
  if (!st.running) return;

  fui::ListItem scan;
  scan.label = st.scanning ? tr(STR_BT_STOP_SCAN) : tr(STR_BT_SCAN);
  scan.actionValue = ROW_SCAN;
  rows.push_back(scan);
  for (uint8_t i = 0; i < found; ++i) {
    fui::ListItem item;
    item.label = labels[i].c_str();
    item.actionValue = static_cast<int16_t>(ROW_FOUND + i);
    rows.push_back(item);
  }
  for (uint8_t i = 0; i < bonds; ++i) {
    fui::ListItem item;
    item.label = labels[found + i].c_str();
    item.value = tr(STR_FORGET_BUTTON);
    item.actionValue = static_cast<int16_t>(ROW_BOND + i);
    if (i == 0) item.sectionHeading = tr(STR_BT_PAIRED_REMOTES);
    rows.push_back(item);
  }
  if (bonds == 0) {
    fui::ListItem none;
    none.label = tr(STR_BT_NO_REMOTES);
    none.enabled = false;
    none.actionValue = ROW_NONE;
    none.sectionHeading = tr(STR_BT_PAIRED_REMOTES);
    rows.push_back(none);
  }
  if (nav.selected >= static_cast<int>(rows.size())) nav.selected = static_cast<int>(rows.size()) - 1;
}

void BleSettingsActivity::activateIndex(const int index) {
  if (index < 0 || index >= static_cast<int>(rows.size()) || !rows[index].enabled) return;
  nav.selected = index;
  app.clearTapFlash();
  const int16_t code = rows[index].actionValue;
  if (code == ROW_ENABLE) {
    SETTINGS.ble.enabled = SETTINGS.ble.enabled ? 0 : 1;
    if (SETTINGS.ble.enabled) {
      bleturner::switchOn();
    } else {
      bleturner::switchOff();
    }
    SETTINGS.saveToFile();
  } else if (code == ROW_SCAN) {
    bleturner::scan(bleturner::status().scanning ? 0 : SCAN_MS);
  } else if (code >= ROW_BOND) {
    const std::string addr = bleturner::bond(static_cast<uint8_t>(code - ROW_BOND)).addr;
    if (bleturner::forget(addr.c_str())) SETTINGS.saveToFile();
  } else if (code >= ROW_FOUND) {
    // The remote chosen here is the one the reader reconnects to.
    const auto peer = bleturner::found(static_cast<uint8_t>(code - ROW_FOUND));
    copyField(SETTINGS.ble.peerAddr, peer.addr, sizeof(SETTINGS.ble.peerAddr));
    copyField(SETTINGS.ble.peerName, peer.name, sizeof(SETTINGS.ble.peerName));
    bleturner::scan(0);
    SETTINGS.saveToFile();
    bleturner::pair(SETTINGS.ble.peerAddr);
  } else {
    return;
  }
  refresh();
  requestUpdate();
}

void BleSettingsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
                                      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
                                      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)),
                                      static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  fui::ListProps props;
  props.items = rows.data();
  props.count = static_cast<uint16_t>(rows.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  syncListViewport(screen, props);
  screen.list(props);
}
