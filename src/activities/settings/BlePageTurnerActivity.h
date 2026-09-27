#pragma once

#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "activities/settings/BleKeyBinding.h"
#include "components/OptionPopup.h"

/**
 * Man cai dat BLE page turner (BTH2).
 *
 * Cau truc bam theo OpdsServerListActivity: mot danh sach FreeInkUI, header chu,
 * footer nut that, popup cho hanh dong phu. Khac o cho danh sach o day KHONG tinh:
 * no doi theo trang thai radio (quang cao tim duoc, bond, link len/xuong), nen
 * rebuildRows() chay lai khi chu ky trang thai doi - khong phai moi lan ve.
 *
 * Cờ FREEINK_CAP_BLE_HID_HOST chi anh huong den THAN ham o BlePageTurnerActivity.cpp;
 * man hinh khong co #if nao, va khi ban dung khong co BLE thi no van vao duoc va
 * hien "khong kha dung" (xem capNhatTrangThai()).
 */
class BlePageTurnerActivity final : public UiListActivity {
 public:
  BlePageTurnerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("BlePageTurner", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  // Ma dong. Danh sach dong doi theo trang thai BLE, nen moi dong mang MOT ma
  // on dinh thay vi chi so mang (chi so chet ngay khi bond/quang cao doi).
  enum RowCode : int16_t {
    ROW_ENABLE = 0,
    ROW_STATUS = 1,
    ROW_SCAN = 2,
    ROW_PAIRED_HEADER = 3,
    // The six bind rows, shown under the status row once the bindings row opens them
    // (rebuildRows decides the display order, not these values). Row code minus
    // ROW_BIND_NEXT plus 1 is the row's blebinding::Action.
    ROW_BIND_NEXT = 4,
    ROW_BIND_PREV = 5,
    ROW_BIND_NEXT_CHAPTER = 6,
    ROW_BIND_PREV_CHAPTER = 7,
    ROW_BIND_READER_MENU = 8,
    ROW_BIND_SAVE_QUOTE = 9,
    ROW_BIND_MENU = 10,
    ROW_PAIRED_BASE = 100,
    ROW_DEVICE_HEADER = 200,
    ROW_DEVICE_BASE = 300,
    ROW_NO_DEVICE = 400,
  };

  int listCount() const override { return static_cast<int>(rowItems_.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onRowLongPress(int index) override;
  bool handleCustomInput() override;
  void stepSelection(int direction) override;
  bool clampAfterNav() override;
  void onBackButton() override;
  const char* headerTitle() const override;

  void rebuildRows();
  void refreshValues();
  void capNhatTrangThai();
  // Chu ky trang thai ma man hinh dang phu thuoc. Mot cho duy nhat de vong poll
  // va cac duong bam tay khong dem lech nhau.
  uint32_t trangThaiSig() const;
  void toggleEnabled();
  void handleScanRow();
  void openPairedPopup(int bondIndex);

  // --- Per-button binding from raw frames (v1.0.14) --------------------------
  // While this screen is open the remote's input belongs to nobody else: both
  // queues are drained. The first press edge of a wait is the button learned, its
  // release tells a tap from a hold, or that the press was the remote's rest frame
  // (then the wait goes on); everything else only shows the last code.
  void readPendingKeys();
  void startBindWait(blebinding::Action action);
  void finishLearn(bool sawRelease, uint32_t heldMs);
  void clearBind(blebinding::Action action);
  // Nhip giu tren mot hang: ca duong nut bam lan duong cam ung deu goi day.
  void clearBindForRow(int index);
  // Value shown on a bind row: a learned code such as "3:1=02", an old usage code
  // "0x51", or "Default".
  std::string bindValue(blebinding::Action action) const;

  std::string statusText_;
  // Dong cua hang Trang thai sau khi ghep thong bao gan nut va ma vua nhan. La
  // thanh vien vi ListItem::value tro vao day, khong phai chuoi tam.
  std::string statusValue_;
  // Values of the six bind rows, same reason: ListItem::value is a pointer.
  std::string bindValues_[6];
  std::vector<freeink::ui::ListItem> rowItems_;
  OptionPopup optionPopup;
  OptionPopup* tiltPopup() override { return &optionPopup; }
  bool rowsDirty = true;
  // The bind rows are open in place of the main rows; Back closes them.
  bool bindMode_ = false;
  // Row the next rebuild selects; -1 keeps the selected row's code where it moved to.
  int16_t selectCode_ = -1;
  uint32_t lastPollMs = 0;
  uint32_t lastStateSig = 0;

  bool bindWaitActive_ = false;
  blebinding::Action bindAction_ = blebinding::Action::NextPage;
  uint32_t bindWaitStartedMs_ = 0;
  // Button being learned: its code and press time, waiting for the release. 0 = none yet.
  uint32_t learnCode_ = 0;
  uint32_t learnPressMs_ = 0;
  uint32_t lastRawCode_ = 0;
  // Thong bao cua luot gan nut (dang cho / da gan / khong nhan duoc) thay cho dong
  // trang thai radio cho toi khi nguoi dung lam viec khac.
  std::string bindNotice_;
};
