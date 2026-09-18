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
    // Hai hang gan nut nam ngay sau hang quet (thu tu hien thi do rebuildRows
    // quyet dinh, khong phai gia tri ma nay).
    ROW_BIND_NEXT = 4,
    ROW_BIND_PREV = 5,
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

  // --- Gan nut (hotfix 18/09/2026) ------------------------------------------
  // Man nay dang mo thi phim cua dieu khien khong thuoc ve ai khac: rut het hang
  // doi, phim dau tien trong luot cho duoc gan, con lai chi de hien ma vua nhan.
  void readPendingKeys();
  void startBindWait(blebinding::Direction direction);
  void clearBind(blebinding::Direction direction);
  // Nhip giu tren mot hang: ca duong nut bam lan duong cam ung deu goi day.
  void clearBindForRow(int index);
  // Gia tri hien o hang gan nut: ma dang gan dang "0x51", hoac "Mac dinh".
  std::string bindValue(blebinding::Direction direction) const;

  std::string statusText_;
  // Dong cua hang Trang thai sau khi ghep thong bao gan nut va ma vua nhan. La
  // thanh vien vi ListItem::value tro vao day, khong phai chuoi tam.
  std::string statusValue_;
  // Gia tri cua hai hang gan nut, cung ly do: ListItem::value la con tro.
  std::string bindNextValue_;
  std::string bindPrevValue_;
  std::vector<freeink::ui::ListItem> rowItems_;
  OptionPopup optionPopup;
  bool rowsDirty = true;
  uint32_t lastPollMs = 0;
  uint32_t lastStateSig = 0;

  bool bindWaitActive_ = false;
  blebinding::Direction bindDirection_ = blebinding::Direction::Next;
  uint32_t bindWaitStartedMs_ = 0;
  uint8_t lastKeyUsage_ = 0;
  // Thong bao cua luot gan nut (dang cho / da gan / khong nhan duoc) thay cho dong
  // trang thai radio cho toi khi nguoi dung lam viec khac.
  std::string bindNotice_;
};
