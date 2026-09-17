#include "BlePageTurnerActivity.h"

#include <BoardConfig.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#if defined(FREEINK_CAP_BLE_HID_HOST) && FREEINK_CAP_BLE_HID_HOST
#include <BleKeyboardHost.h>
#endif

#include <cstring>
#include <string>

#include "CrossPointSettings.h"
#include "BlePageTurnerRuntime.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"

// Cau noi DUY NHAT giua man cai dat va host BLE cua SDK. Cờ FREEINK_CAP_BLE_HID_HOST
// chi doi THAN ham o day; khong #if nao bao quanh man hinh. Khi ban dung khong co
// BLE, moi ham tra ve "khong co thiet bi" va man hinh hien "khong kha dung" thay vi
// treo. Muon cam that: them `BleKeyboardHost=symlink://freeink-sdk/libs/network/
// BleKeyboardHost` vao lib_deps va -DFREEINK_CAP_BLE_HID_HOST=1 (platformio.ini).
namespace {

// Cap BLE HID host cua ban dung. Doc bang #if defined() chu khong tin mot
// include: tren simulator, <BoardConfig.h> do goi simulator cung cap va KHONG
// mang macro cua SDK, nen "khong dinh nghia" phai hieu la "cap dang tat" -
// dung quy uoc ma chinh BoardConfig.h cua SDK dat ra (mac dinh 0).
#if defined(FREEINK_CAP_BLE_HID_HOST) && FREEINK_CAP_BLE_HID_HOST
#define BLE_UI_CAP_HID_HOST 1
#else
#define BLE_UI_CAP_HID_HOST 0
#endif

namespace backend {

// Ban dung co BLE HID host hay khong. Hang so luc dich, khong cap phat, de man
// hinh biet ngay no phai noi "khong kha dung" ma khong phai thu init radio.
constexpr bool compiledIn() { return BLE_UI_CAP_HID_HOST != 0; }

#if BLE_UI_CAP_HID_HOST

bool begin(GfxRenderer& renderer) { return freeink::ble::begin(renderer); }
bool end() { return BleHid.end(); }
bool stopping() { return BleHid.isStopping(); }
bool running() { return BleHid.isRunning(); }
void poll() { BleHid.poll(); }
bool scanning() { return BleHid.isScanning(); }
void startScan() { BleHid.startScan(15000); }
void stopScan() { BleHid.stopScan(); }
uint8_t bondCount() { return BleHid.pairedCount(); }
const char* bondAddr(const uint8_t i) { return BleHid.paired(i).addr; }
const char* bondName(const uint8_t i) { return BleHid.paired(i).name; }
uint8_t deviceCount() { return BleHid.deviceCount(); }
const char* deviceAddr(const uint8_t i) { return BleHid.device(i).addr; }
const char* deviceName(const uint8_t i) { return BleHid.device(i).name; }
bool connected() { return BleHid.isConnected(); }
bool connecting() { return BleHid.isConnecting(); }
const char* connectedName() { return BleHid.connectedName(); }
bool connect(const char* addr) { return BleHid.connect(addr); }
void disconnect() { BleHid.disconnect(); }
void forget(const char* addr) { BleHid.forget(addr); }
bool takeFailure(char* out, const size_t outLen) { return BleHid.takeConnectFailure(out, outLen); }

// Mot phim bam trong luc man cai dat dang mo khong duoc lat trang khi nguoi doc
// quay lai: hang doi phai rong truoc khi roi man.
void drainKeys() {
  freeink::KeyEvent ev;
  while (BleHid.popKey(ev)) {
  }
}

#else

bool begin(GfxRenderer&) { return false; }
bool end() { return true; }
bool stopping() { return false; }
bool running() { return false; }
void poll() {}
bool scanning() { return false; }
void startScan() {}
void stopScan() {}
uint8_t bondCount() { return 0; }
const char* bondAddr(const uint8_t) { return ""; }
const char* bondName(const uint8_t) { return ""; }
uint8_t deviceCount() { return 0; }
const char* deviceAddr(const uint8_t) { return ""; }
const char* deviceName(const uint8_t) { return ""; }
bool connected() { return false; }
bool connecting() { return false; }
const char* connectedName() { return ""; }
bool connect(const char*) { return false; }
void disconnect() {}
void forget(const char*) {}
bool takeFailure(char*, const size_t) { return false; }
void drainKeys() {}

#endif

}  // namespace backend

void chupChuoi(char* dest, const char* src, const size_t maxLen) {
  strncpy(dest, src, maxLen - 1);
  dest[maxLen - 1] = '\0';
}

}  // namespace

namespace fui = freeink::ui;

void BlePageTurnerActivity::onEnter() {
  UiListActivity::onEnter();
  nav.selected = 0;
  lastPollMs = millis();
  lastStateSig = 0;
  rowsDirty = true;
  // Trang thai phai dung NGAY lan ve dau tien, khong doi mot nhip poll.
  capNhatTrangThai();
  requestUpdate();
}

void BlePageTurnerActivity::loop() {
  UiListActivity::loop();

  // Phim cua page turner khong thuoc ve man nay; bo chung truoc khi roi man.
  backend::drainKeys();

  const uint32_t now = millis();
  if (now - lastPollMs < 250) return;  // nhip 4 lan/giay: du muot cho e-ink, khong quay CPU
  lastPollMs = now;
  if (SETTINGS.blePageTurnerEnabled && backend::running()) backend::poll();

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
         (backend::running() ? 16u : 0u) | (SETTINGS.blePageTurnerEnabled ? 32u : 0u) |
         (static_cast<uint32_t>(backend::bondCount()) << 8) |
         (static_cast<uint32_t>(backend::deviceCount()) << 16);
}

void BlePageTurnerActivity::capNhatTrangThai() {
  char failure[48];
  if (backend::stopping()) {
    statusText_ = tr(STR_BLE_STOPPING);
  } else if (backend::takeFailure(failure, sizeof(failure))) {
    statusText_ = failure;
  } else if (!backend::compiledIn()) {
    statusText_ = tr(STR_BLE_UNAVAILABLE);
  } else if (!SETTINGS.blePageTurnerEnabled) {
    statusText_ = tr(STR_STATE_OFF);
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
          static_cast<int>(SETTINGS.blePageTurnerEnabled), backend::bondCount(), backend::deviceCount());
  lastStateSig = trangThaiSig();
}

void BlePageTurnerActivity::rebuildRows() {
  rowItems_.clear();

  const auto them = [this](const char* label, const int16_t code) {
    fui::ListItem item;
    item.label = label;
    item.actionValue = code;
    rowItems_.push_back(item);
  };

  them(tr(STR_BLE_PAGE_TURNER), ROW_ENABLE);
  them(tr(STR_BLE_STATUS), ROW_STATUS);
  them(tr(STR_BLE_SCAN), ROW_SCAN);

  them(tr(STR_BLE_PAIRED_DEVICES), ROW_PAIRED_HEADER);
  const uint8_t bonds = backend::bondCount();
  for (uint8_t i = 0; i < bonds; i++) {
    them(backend::bondName(i)[0] != '\0' ? backend::bondName(i) : backend::bondAddr(i), ROW_PAIRED_BASE + i);
  }
  if (bonds == 0) them(tr(STR_BLE_NO_DEVICES), ROW_NO_DEVICE);

  // Thiet bi quang cao duoc chi hien khi CO: mot dong "khong tim thay" thu hai se
  // lam nguoi doc tuong dang co mot muc nua.
  const uint8_t found = backend::deviceCount();
  for (uint8_t i = 0; i < found; i++) {
    them(backend::deviceName(i)[0] != '\0' ? backend::deviceName(i) : backend::deviceAddr(i), ROW_DEVICE_BASE + i);
  }
}

void BlePageTurnerActivity::refreshValues() {
  if (rowItems_.size() < 3) return;
  rowItems_[0].value = SETTINGS.blePageTurnerEnabled ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
  rowItems_[1].value = statusText_.c_str();
  rowItems_[2].label = backend::scanning() ? tr(STR_BLE_STOP_SCAN) : tr(STR_BLE_SCAN);
}

void BlePageTurnerActivity::toggleEnabled() {
  SETTINGS.blePageTurnerEnabled = SETTINGS.blePageTurnerEnabled ? 0 : 1;
  if (SETTINGS.blePageTurnerEnabled) {
    // Y dinh cua nguoi dung duoc ghi lai du lan init nay co thanh cong hay khong:
    // "da bat" va "radio dang chay" la hai chuyen khac nhau, va man hinh noi ro
    // chuyen nao dang xay ra. (Ban dung khong co BLE: begin() tra false.)
    if (!backend::begin(renderer)) {
      LOG_ERR("BLE", "BLE HID host begin() failed; preference kept, radio not running");
    }
  } else {
    backend::stopScan();
    if (!backend::end()) LOG_INF("BLE", "BLE shutdown pending; input loop will finish cleanup");
  }
  SETTINGS.saveToFile();
}

void BlePageTurnerActivity::handleScanRow() {
  if (!SETTINGS.blePageTurnerEnabled) return;  // chua bat thi khong co gi de quet
  if (!backend::running() && !backend::begin(renderer)) return;  // ban dung khong co BLE
  if (backend::scanning()) {
    backend::stopScan();
    return;
  }
  backend::startScan();
}

void BlePageTurnerActivity::openPairedPopup(const int bondIndex) {
  if (bondIndex < 0 || bondIndex >= backend::bondCount()) return;
  const std::string addr = backend::bondAddr(bondIndex);
  const bool dangNoi = backend::connected();
  const StrId options[2] = {dangNoi ? StrId::STR_BLE_DISCONNECT : StrId::STR_BLE_CONNECT, StrId::STR_BLE_FORGET};
  optionPopup.show(StrId::STR_BLE_PAIRED_DEVICES, options, 2, 0, [this, addr, dangNoi](const int idx) {
    if (idx == 0) {
      if (dangNoi) {
        backend::disconnect();
      } else {
        chupChuoi(SETTINGS.blePeerAddr, addr.c_str(), sizeof(SETTINGS.blePeerAddr));
        SETTINGS.saveToFile();
        backend::connect(SETTINGS.blePeerAddr);
      }
    } else {
      backend::forget(addr.c_str());
      // Mot dia chi vua bi quen ma con nam trong lua chon da luu la mot lan ket
      // noi lai vao khoang khong o lan sau.
      if (strncmp(SETTINGS.blePeerAddr, addr.c_str(), sizeof(SETTINGS.blePeerAddr)) == 0) {
        SETTINGS.blePeerAddr[0] = '\0';
        SETTINGS.blePeerName[0] = '\0';
        SETTINGS.saveToFile();
      }
    }
    capNhatTrangThai();
    rowsDirty = true;
    requestUpdate();
  });
  requestUpdate();
}

void BlePageTurnerActivity::activateIndex(const int index) {
  nav.selected = index;
  // Mo popup hoac doi trang thai radio deu ve mot be mat khac: vet sang con lai
  // se lam xam mot o khong lien quan.
  app.clearTapFlash();
  if (index < 0 || index >= static_cast<int>(rowItems_.size())) return;

  const int16_t code = rowItems_[index].actionValue;
  if (code == ROW_ENABLE) {
    toggleEnabled();
  } else if (code == ROW_SCAN) {
    handleScanRow();
  } else if (code >= ROW_PAIRED_BASE && code < ROW_PAIRED_BASE + static_cast<int16_t>(backend::bondCount())) {
    openPairedPopup(code - ROW_PAIRED_BASE);
    return;  // popup tu lo phan ve
  } else if (code >= ROW_DEVICE_BASE && code < ROW_DEVICE_BASE + static_cast<int16_t>(backend::deviceCount())) {
    const int dev = code - ROW_DEVICE_BASE;
    backend::stopScan();
    chupChuoi(SETTINGS.blePeerAddr, backend::deviceAddr(dev), sizeof(SETTINGS.blePeerAddr));
    chupChuoi(SETTINGS.blePeerName, backend::deviceName(dev), sizeof(SETTINGS.blePeerName));
    SETTINGS.saveToFile();
    backend::connect(SETTINGS.blePeerAddr);
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

const char* BlePageTurnerActivity::headerTitle() const { return tr(STR_BLE_PAGE_TURNER); }

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
    rebuildRows();
    rowsDirty = false;
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
