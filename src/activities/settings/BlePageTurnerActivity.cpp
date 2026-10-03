#include "BlePageTurnerActivity.h"

#include <BlePageTurner.h>
#include <BoardConfig.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <cstring>
#include <string>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/reader/ReaderUtils.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"

// One hold rule, one number: a remote hold and a front-button hold skip chapters alike.
static_assert(bleturner::kHoldMs == ReaderUtils::SKIP_HOLD_MS, "remote hold threshold drifted from the button one");

// Cau noi DUY NHAT giua man cai dat va radio: moi viec di qua lib/BlePageTurner, man
// hinh khong goi SDK. Ban dung khong co BLE thi module tra "khong co thiet bi" va man
// hinh hien "khong kha dung" thay vi treo.
namespace {

namespace backend {

bool compiledIn() { return bleturner::status().compiledIn; }
bool begin() { return bleturner::switchOn(); }
bool switchOff() { return bleturner::switchOff(); }
bool stopping() { return bleturner::status().stopping; }
bool running() { return bleturner::status().running; }
bool idleStopped() { return bleturner::status().idleStopped; }
// A remote being paired that links becomes the chosen one (main loop saves the settings).
void poll() { bleturner::service(); }
bool scanning() { return bleturner::status().scanning; }
void startScan() { bleturner::scan(15000); }
void stopScan() { bleturner::scan(0); }
uint8_t bondCount() { return bleturner::bondCount(); }
const char* bondAddr(const uint8_t i) { return bleturner::bond(i).addr; }
const char* bondName(const uint8_t i) { return bleturner::bond(i).name; }
uint8_t deviceCount() { return bleturner::foundCount(); }
const char* deviceAddr(const uint8_t i) { return bleturner::found(i).addr; }
const char* deviceName(const uint8_t i) { return bleturner::found(i).name; }
bool connected() { return bleturner::status().connected; }
bool connecting() { return bleturner::status().connecting; }
const char* connectedName() { return bleturner::linked().name; }
const char* connectedAddr() { return bleturner::linked().addr; }
bool pair(const char* addr, const char* name) { return bleturner::pair(addr, name); }
void disconnect() { bleturner::disconnect(); }
// Quen ca bond, bang nut cua no va lua chon da luu neu trung: true = cai dat da doi.
bool forget(const char* addr) { return bleturner::forget(addr); }
bool takeFailure(char* out, const size_t outLen) { return bleturner::takeConnectFailure(out, outLen); }

// One queued raw edge: button code (see BleKeyBinding.h), press or release, when, and on a
// release whether the press was the remote's rest frame. Key events are drained on the way:
// left queued, the reader would turn pages for old presses when the user goes back.
bool takeRaw(uint32_t& code, bool& pressed, uint32_t& atMs, bool& wasRest) {
  bleturner::Event ev;
  if (!bleturner::pollEvent(ev)) return false;
  code = ev.code;
  pressed = ev.pressed;
  atMs = ev.atMs;
  wasRest = ev.wasRest;
  return true;
}

// Trinh doc da thu bat radio va bi hoan vi RAM: hang Trang thai phai noi that.
bool readerDeferred() { return bleturner::status().readerDeferred; }

}  // namespace backend

}  // namespace

namespace fui = freeink::ui;

void BlePageTurnerActivity::onEnter() {
  UiListActivity::onEnter();
  nav.selected = 0;
  lastPollMs = millis();
  lastStateSig = 0;
  rowsDirty = true;
  // Khoi dong opt-in da luu khi vao cai dat. Neu radio tu tat vi nhan roi,
  // giu ly do do den khi nguoi dung chu dong bat, quet hoac ket noi lai.
  if (SETTINGS.ble.enabled && backend::compiledIn() && !backend::running() && !backend::stopping() &&
      !backend::idleStopped()) {
    if (!backend::begin()) {
      LOG_ERR("BLE", "Saved opt-in could not start the radio on the settings screen; switching it off");
      SETTINGS.ble.enabled = 0;
      SETTINGS.saveToFile();
    }
  }
  // Trang thai phai dung NGAY lan ve dau tien, khong doi mot nhip poll.
  capNhatTrangThai();
  requestUpdate();
}

void BlePageTurnerActivity::loop() {
  UiListActivity::loop();

  // Phim cua page turner khong thuoc ve man nay: rut het hang doi. Trong luot gan
  // nut thi chinh phim do la thu can doc, ngoai luot do thi chi de hien ma vua nhan.
  readPendingKeys();
  if (bindWaitActive_ && learnCode_ == 0 && millis() - bindWaitStartedMs_ >= bleturner::kWaitMs) {
    bindWaitActive_ = false;
    bindNotice_ = tr(STR_BLE_BIND_NONE);
    LOG_INF("BLE", "Bind wait ended with no key");
    rowsDirty = true;
    requestUpdate();
  }

  // Duong giu nut cua may nut bam. Do duoc tren gia lap X3: day la duong chay,
  // con onRowLongPress() khong he duoc goi o may khong cam ung. Lop nen chi doc
  // nhip giu khi man co bat tinh nang ghim, ma man nay khong bat.
  if (!optionPopup.isActive() && mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, 700)) {
    clearBindForRow(nav.selected);
  }

  const uint32_t now = millis();
  if (now - lastPollMs < 250) return;  // nhip 4 lan/giay: du muot cho e-ink, khong quay CPU
  lastPollMs = now;
  if (SETTINGS.ble.enabled && backend::running()) backend::poll();

  if (trangThaiSig() == lastStateSig) return;  // khong co gi doi thi khong ve lai: e-ink tra gia cho moi khung
  // capNhatTrangThai() chot luon lastStateSig, nen mot nhip bam tay da cap nhat
  // trang thai se khong bi dem lai lan nua o vong poll ke tiep.
  capNhatTrangThai();
  rowsDirty = true;
  requestUpdate();
}

uint32_t BlePageTurnerActivity::trangThaiSig() const {
  return (backend::scanning() ? 1u : 0u) | (backend::connected() ? 2u : 0u) |
         (backend::connecting() ? 4u : 0u) | (backend::stopping() ? 8u : 0u) |
         (backend::running() ? 16u : 0u) | (SETTINGS.ble.enabled ? 32u : 0u) |
         (backend::idleStopped() ? 64u : 0u) | (backend::readerDeferred() ? 128u : 0u) |
         (static_cast<uint32_t>(backend::bondCount()) << 8) |
         (static_cast<uint32_t>(backend::deviceCount()) << 16);
}

void BlePageTurnerActivity::capNhatTrangThai() {
  char failure[48];
  const bool hasFailure = backend::takeFailure(failure, sizeof(failure));
  if (backend::stopping()) {
    statusText_ = tr(STR_BLE_STOPPING);
  } else if (!backend::compiledIn()) {
    statusText_ = tr(STR_BLE_UNAVAILABLE);
  } else if (!SETTINGS.ble.enabled) {
    statusText_ = tr(STR_STATE_OFF);
  } else if (backend::idleStopped()) {
    statusText_ = tr(STR_BLE_IDLE_STOPPED);
  } else if (hasFailure) {
    statusText_ = failure;
  } else if (backend::readerDeferred()) {
    // Radio co the dang chay o man nay nhung lan thu bat trong trinh doc da bi
    // hoan vi RAM - noi that thay vi hien "BAT".
    statusText_ = tr(STR_BLE_READER_LOW_RAM);
  } else if (!backend::running()) {
    statusText_ = tr(STR_BLE_START_FAILED);
  } else if (backend::connected()) {
    statusText_ = backend::connectedName();
  } else if (backend::connecting()) {
    statusText_ = tr(STR_CONNECTING);
  } else if (backend::scanning()) {
    statusText_ = tr(STR_SCANNING);
  } else {
    statusText_ = tr(STR_STATE_ON);
  }
  // Mot dong log cho moi lan trang thai THAT SU doi: bai kiem mo phong doc duoc
  // dung chuoi nay, nen no la bang chung chu khong phai trang tri.
  LOG_INF("BLE", "BlePageTurner status=%s enabled=%d bonds=%u devices=%u", statusText_.c_str(),
          static_cast<int>(SETTINGS.ble.enabled), backend::bondCount(), backend::deviceCount());
  lastStateSig = trangThaiSig();
}

void BlePageTurnerActivity::rebuildRows() {
  // A found device or bond appearing moves the rows: the selection stays on its row.
  int16_t keep = selectCode_;
  if (keep < 0 && nav.selected >= 0 && nav.selected < static_cast<int>(rowItems_.size())) {
    keep = rowItems_[nav.selected].actionValue;
  }
  selectCode_ = -1;
  rowItems_.clear();

  const auto them = [this](const char* label, const int16_t code) {
    fui::ListItem item;
    item.label = label;
    item.actionValue = code;
    rowItems_.push_back(item);
  };

  if (bindMode_) {
    them(tr(STR_BLE_STATUS), ROW_STATUS);
    rowItems_.back().enabled = false;
    them(tr(STR_BLE_BIND_NEXT), ROW_BIND_NEXT);
    them(tr(STR_BLE_BIND_PREV), ROW_BIND_PREV);
    them(tr(STR_BLE_BIND_NEXT_CHAPTER), ROW_BIND_NEXT_CHAPTER);
    them(tr(STR_BLE_BIND_PREV_CHAPTER), ROW_BIND_PREV_CHAPTER);
    them(tr(STR_BLE_BIND_READER_MENU), ROW_BIND_READER_MENU);
    them(tr(STR_BLE_BIND_SAVE_QUOTE), ROW_BIND_SAVE_QUOTE);
  } else {
    them(tr(STR_BLE_PAGE_TURNER), ROW_ENABLE);
    them(tr(STR_BLE_STATUS), ROW_STATUS);
    rowItems_.back().enabled = false;
    them(tr(STR_BLE_SCAN), ROW_SCAN);
    // Thiet bi quang cao duoc chi hien khi CO, ngay duoi dong Quet: mot dong "khong
    // tim thay" thu hai se lam nguoi doc tuong dang co mot muc nua.
    const uint8_t found = backend::deviceCount();
    for (uint8_t i = 0; i < found; i++) {
      them(backend::deviceName(i)[0] != '\0' ? backend::deviceName(i) : backend::deviceAddr(i), ROW_DEVICE_BASE + i);
    }
    them(tr(STR_BLE_BIND_BUTTONS), ROW_BIND_MENU);

    const uint8_t bonds = backend::bondCount();
    for (uint8_t i = 0; i < bonds; i++) {
      them(backend::bondName(i)[0] != '\0' ? backend::bondName(i) : backend::bondAddr(i), ROW_PAIRED_BASE + i);
      if (i == 0) rowItems_.back().sectionHeading = tr(STR_BLE_PAIRED_DEVICES);
    }
    if (bonds == 0) {
      them(tr(STR_BLE_NO_DEVICES), ROW_NO_DEVICE);
      rowItems_.back().enabled = false;
      rowItems_.back().sectionHeading = tr(STR_BLE_PAIRED_DEVICES);
    }
  }
  for (int i = 0; keep >= 0 && i < static_cast<int>(rowItems_.size()); ++i) {
    if (rowItems_[i].actionValue == keep) {
      nav.selected = i;
      break;
    }
  }
  clampAfterNav();
}

void BlePageTurnerActivity::stepSelection(const int direction) {
  const int count = listCount();
  for (int i = 0; i < count; ++i) {
    UiListActivity::stepSelection(direction);
    if (rowItems_[nav.selected].enabled) return;
  }
}

bool BlePageTurnerActivity::clampAfterNav() {
  if (rowItems_.empty()) return false;
  const int previous = nav.selected;
  nav.selected = kepConTro(nav.selected, listCount());
  // Page moves and holds can land on information rows too. Keep one focus
  // policy for those paths and for a changing bond/discovery list.
  if (!rowItems_[nav.selected].enabled) stepSelection(1);
  return nav.selected != previous;
}

void BlePageTurnerActivity::refreshValues() {
  // Hang Trang thai: thong bao cua luot gan nut neu dang co, roi den trang thai
  // radio, va duoi cung la ma vua nhan khi man con mo.
  statusValue_ = bindNotice_.empty() ? statusText_ : bindNotice_;
  if (lastRawCode_ != 0) {
    char ma[16];
    char duoi[48];
    bleturner::formatCode(ma, sizeof(ma), lastRawCode_);
    snprintf(duoi, sizeof(duoi), tr(STR_BLE_LAST_KEY), ma);
    statusValue_ += " - ";  // gop hai manh thanh mot dong
    statusValue_ += duoi;
  }
  for (auto& item : rowItems_) {
    const int16_t code = item.actionValue;
    if (code == ROW_ENABLE) {
      item.value = SETTINGS.ble.enabled ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    } else if (code == ROW_STATUS) {
      item.value = statusValue_.c_str();
    } else if (code == ROW_SCAN) {
      item.label = backend::scanning() ? tr(STR_BLE_STOP_SCAN) : tr(STR_BLE_SCAN);
    } else if (code >= ROW_BIND_NEXT && code <= ROW_BIND_SAVE_QUOTE) {
      const int i = code - ROW_BIND_NEXT;
      bindValues_[i] = bindValue(static_cast<bleturner::Action>(i + 1));
      item.value = bindValues_[i].c_str();
    }
  }
}

std::string BlePageTurnerActivity::bindValue(const bleturner::Action action) const {
  const bleturner::RemoteTable* bang =
      backend::connected() ? bleturner::tableFor(SETTINGS.ble.remotes, SETTINGS.ble.remoteCount,
                                                 backend::connectedAddr(), backend::connectedName())
                           : nullptr;
  bleturner::Binding o = 0;
  char ma[16];
  if (bang != nullptr && bleturner::find(*bang, action, o)) {
    bleturner::formatCode(ma, sizeof(ma), o);
    std::string giaTri = ma;
    if (o & bleturner::kHoldBit) {
      giaTri += ' ';
      giaTri += tr(STR_BLE_BIND_HOLD);
    }
    return giaTri;
  }
  // A button the table does not name still turns pages by its old usage code: show it.
  const uint8_t cu = action == bleturner::Action::NextPage   ? SETTINGS.ble.nextKeyUsage
                     : action == bleturner::Action::PrevPage ? SETTINGS.ble.prevKeyUsage
                                                             : bleturner::kUsageNone;
  if (cu == bleturner::kUsageNone) return tr(STR_BLE_BIND_DEFAULT);
  snprintf(ma, sizeof(ma), "0x%02X", cu);
  return std::string(ma);
}

void BlePageTurnerActivity::readPendingKeys() {
  uint32_t code = 0;
  bool pressed = false;
  uint32_t atMs = 0;
  bool wasRest = false;
  while (backend::takeRaw(code, pressed, atMs, wasRest)) {
    if (pressed) {
      lastRawCode_ = code;
      if (bindWaitActive_ && learnCode_ == 0) {
        learnCode_ = code;
        learnPressMs_ = atMs;
      }
    } else if (wasRest) {
      // That "press" was the remote idling on a non-zero byte, read before its rest frame
      // was known: no button. Learning keeps waiting for the next press.
      if (lastRawCode_ == code) lastRawCode_ = 0;
      if (bindWaitActive_ && code == learnCode_) {
        learnCode_ = 0;
        LOG_INF("BLE", "Rest frame ignored while learning");
      }
    } else if (bindWaitActive_ && code == learnCode_) {
      finishLearn(true, atMs - learnPressMs_);
    }
    rowsDirty = true;
    requestUpdate();
  }
  // No release within kReleaseWaitMs: the remote does not report this button coming
  // up, so it is learned as a tap.
  if (bindWaitActive_ && learnCode_ != 0 && millis() - learnPressMs_ >= bleturner::kReleaseWaitMs) {
    finishLearn(false, 0);
  }
}

void BlePageTurnerActivity::finishLearn(const bool sawRelease, const uint32_t heldMs) {
  const uint32_t code = learnCode_;
  const bool giu = sawRelease && heldMs >= bleturner::kHoldMs;
  bindWaitActive_ = false;
  learnCode_ = 0;
  bleturner::RemoteTable* bang = bleturner::editableTable(SETTINGS.ble.remotes, SETTINGS.ble.remoteCount,
                                                          backend::connectedAddr(), backend::connectedName());
  char ma[16];
  bleturner::formatCode(ma, sizeof(ma), code);
  if (!backend::connected()) {
    // The link dropped while waiting for the release: nothing to bind it to.
    bindNotice_ = tr(STR_BLE_BIND_NONE);
    LOG_INF("BLE", "Link lost while learning %s", ma);
  } else if (bang == nullptr || !bleturner::learn(*bang, bindAction_, code, giu)) {
    bindNotice_ = tr(STR_BLE_BIND_FULL);
    LOG_INF("BLE", "Binding table full; %s not bound to %s", ma, bleturner::actionName(bindAction_));
  } else {
    SETTINGS.saveToFile();
    std::string nut = ma;
    if (giu) {
      nut += ' ';
      nut += tr(STR_BLE_BIND_HOLD);
    }
    char thongBao[64];
    snprintf(thongBao, sizeof(thongBao), tr(STR_BLE_BIND_DONE), nut.c_str());
    bindNotice_ = thongBao;
    LOG_INF("BLE", "Bound %s%s to %s (held %u ms, release %d)", ma, giu ? " hold" : "",
            bleturner::actionName(bindAction_), static_cast<unsigned>(heldMs), sawRelease ? 1 : 0);
  }
  rowsDirty = true;
  requestUpdate();
}

void BlePageTurnerActivity::startBindWait(const bleturner::Action action) {
  bindAction_ = action;
  bindWaitActive_ = true;
  bindWaitStartedMs_ = millis();
  learnCode_ = 0;
  bindNotice_ = tr(STR_BLE_BIND_WAIT);
  LOG_INF("BLE", "Waiting for a button to bind to %s", bleturner::actionName(action));
  rowsDirty = true;
  requestUpdate();
}

void BlePageTurnerActivity::clearBind(const bleturner::Action action) {
  // Connected remote: drop this action's slot from its table (a built-in default is
  // copied out first, so it does not come back next time). The two page rows also
  // clear the old usage code, as before.
  if (backend::connected()) {
    bleturner::RemoteTable* bang = bleturner::editableTable(SETTINGS.ble.remotes, SETTINGS.ble.remoteCount,
                                                            backend::connectedAddr(), backend::connectedName());
    if (bang != nullptr) bleturner::clearAction(*bang, action);
  }
  if (action == bleturner::Action::NextPage) SETTINGS.ble.nextKeyUsage = bleturner::kUsageNone;
  if (action == bleturner::Action::PrevPage) SETTINGS.ble.prevKeyUsage = bleturner::kUsageNone;
  SETTINGS.saveToFile();
  bindWaitActive_ = false;
  learnCode_ = 0;
  bindNotice_ = tr(STR_BLE_BIND_CLEAR);
  LOG_INF("BLE", "Cleared the %s binding", bleturner::actionName(action));
  rowsDirty = true;
  requestUpdate();
}

void BlePageTurnerActivity::toggleEnabled() {
  SETTINGS.ble.enabled = SETTINGS.ble.enabled ? 0 : 1;
  if (SETTINGS.ble.enabled) {
    // Y dinh cua nguoi dung duoc ghi lai du lan init nay co thanh cong hay khong:
    // "da bat" va "radio dang chay" la hai chuyen khac nhau, va man hinh noi ro
    // chuyen nao dang xay ra. (Ban dung khong co BLE: begin() tra false.)
    if (!backend::begin()) {
      LOG_ERR("BLE", "BLE HID host begin() failed; preference kept, radio not running");
    }
  } else {
    backend::stopScan();
    if (!backend::switchOff()) LOG_INF("BLE", "BLE shutdown pending; input loop will finish cleanup");
  }
  SETTINGS.saveToFile();
}

void BlePageTurnerActivity::handleScanRow() {
  if (!SETTINGS.ble.enabled) return;  // chua bat thi khong co gi de quet
  if (!backend::begin()) return;  // ban dung khong co BLE
  if (backend::scanning()) {
    backend::stopScan();
    return;
  }
  backend::startScan();
}

void BlePageTurnerActivity::openPairedPopup(const int bondIndex) {
  if (bondIndex < 0 || bondIndex >= backend::bondCount()) return;
  const std::string addr = backend::bondAddr(bondIndex);
  const std::string name = backend::bondName(bondIndex);
  const bool dangNoi = backend::connected();
  const StrId options[2] = {dangNoi ? StrId::STR_BLE_DISCONNECT : StrId::STR_BLE_CONNECT, StrId::STR_BLE_FORGET};
  optionPopup.show(StrId::STR_BLE_PAIRED_DEVICES, options, 2, 0, [this, addr, name, dangNoi](const int idx) {
    if (idx == 0) {
      if (dangNoi) {
        backend::disconnect();
      } else {
        // Chi thanh remote da chon khi noi duoc (bleturner::service): noi hong giu nguyen remote cu.
        backend::pair(addr.c_str(), name.c_str());
      }
    } else if (backend::forget(addr.c_str())) {
      SETTINGS.saveToFile();
    }
    capNhatTrangThai();
    rowsDirty = true;
    requestUpdate();
  });
  requestUpdate();
}

void BlePageTurnerActivity::activateIndex(const int index) {
  if (index < 0 || index >= static_cast<int>(rowItems_.size()) || !rowItems_[index].enabled) return;
  nav.selected = index;
  // Mo popup hoac doi trang thai radio deu ve mot be mat khac: vet sang con lai
  // se lam xam mot o khong lien quan.
  app.clearTapFlash();
  const int16_t code = rowItems_[index].actionValue;
  const bool hangGan = code >= ROW_BIND_NEXT && code <= ROW_BIND_SAVE_QUOTE;
  // Mot nhip vao hang khac la nguoi dung doi y: thong bao cu va luot cho cu het hieu luc.
  if (!hangGan) {
    bindWaitActive_ = false;
    bindNotice_.clear();
  }
  if (code == ROW_ENABLE) {
    toggleEnabled();
  } else if (code == ROW_BIND_MENU) {
    bindMode_ = true;
    selectCode_ = ROW_BIND_NEXT;
  } else if (code == ROW_SCAN) {
    handleScanRow();
  } else if (hangGan) {
    startBindWait(static_cast<bleturner::Action>(code - ROW_BIND_NEXT + 1));
    return;  // startBindWait da ve lai man
  } else if (code >= ROW_PAIRED_BASE && code < ROW_PAIRED_BASE + static_cast<int16_t>(backend::bondCount())) {
    openPairedPopup(code - ROW_PAIRED_BASE);
    return;  // popup tu lo phan ve
  } else if (code >= ROW_DEVICE_BASE && code < ROW_DEVICE_BASE + static_cast<int16_t>(backend::deviceCount())) {
    const int dev = code - ROW_DEVICE_BASE;
    backend::stopScan();
    // Chi thanh remote da chon khi noi duoc (bleturner::service): ghep hong giu nguyen remote cu.
    backend::pair(backend::deviceAddr(dev), backend::deviceName(dev));
  } else {
    return;  // dong tieu de / dong "khong tim thay": khong co viec gi
  }

  capNhatTrangThai();
  rowsDirty = true;
  requestUpdate();
}

bool BlePageTurnerActivity::handleCustomInput() {
  return optionPopup.handleInput(mappedInput, [this] { requestUpdate(); });
}

// Duong giu nut cua may cam ung, do lop nen phat su kien hang co co longPress.
void BlePageTurnerActivity::onRowLongPress(const int index) { clearBindForRow(index); }

// Mot quyet dinh, mot ham: hai duong nhip giu o tren deu di qua day, nen luat
// "giu tren hang gan nut la xoa gan" chi duoc phat bieu mot lan.
void BlePageTurnerActivity::clearBindForRow(const int index) {
  if (index < 0 || index >= static_cast<int>(rowItems_.size())) return;
  const int16_t code = rowItems_[index].actionValue;
  if (code >= ROW_BIND_NEXT && code <= ROW_BIND_SAVE_QUOTE) {
    clearBind(static_cast<bleturner::Action>(code - ROW_BIND_NEXT + 1));
  }
}

void BlePageTurnerActivity::onBackButton() {
  if (!bindMode_) {
    finish();
    return;
  }
  // A wait still open ends with the bindings; the notice goes with them.
  bindMode_ = false;
  bindWaitActive_ = false;
  learnCode_ = 0;
  bindNotice_.clear();
  selectCode_ = ROW_BIND_MENU;
  rowsDirty = true;
  requestUpdate();
}

const char* BlePageTurnerActivity::headerTitle() const {
  return bindMode_ ? tr(STR_BLE_BIND_BUTTONS) : tr(STR_BLE_PAGE_TURNER);
}

void BlePageTurnerActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Cung bien noi dung voi OpdsServerListActivity: duoi dai header, tren dai nut.
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMarginFromScreen(
      fui::Insets{static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
                  static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
                  static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height) + metrics.buttonHintsHeight),
                  static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  if (rowsDirty) {
    // Cleared before the rebuild: a row tapped on the loop while it runs (the bindings
    // opening, Back closing them) marks the rows again instead of being lost.
    rowsDirty = false;
    rebuildRows();
  }
  refreshValues();

  fui::ListProps props;
  props.items = rowItems_.data();
  props.count = static_cast<uint16_t>(rowItems_.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // nut vat ly do loop() cua lop nen lo
  props.valueInset = 8;
  syncListViewport(screen, props);
  screen.list(props);
}

void BlePageTurnerActivity::render(RenderLock&& lock) {
  if (optionPopup.processRender(renderer, mappedInput)) return;
  UiListActivity::render(std::move(lock));
}
