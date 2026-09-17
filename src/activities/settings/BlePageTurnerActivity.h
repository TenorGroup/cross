#pragma once

#include <string>
#include <vector>

#include "activities/UiListActivity.h"
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
    ROW_PAIRED_BASE = 100,
    ROW_DEVICE_HEADER = 200,
    ROW_DEVICE_BASE = 300,
    ROW_NO_DEVICE = 400,
  };

  int listCount() const override { return static_cast<int>(rowItems_.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
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

  std::string statusText_;
  std::vector<freeink::ui::ListItem> rowItems_;
  OptionPopup optionPopup;
  bool rowsDirty = true;
  uint32_t lastPollMs = 0;
  uint32_t lastStateSig = 0;
};
