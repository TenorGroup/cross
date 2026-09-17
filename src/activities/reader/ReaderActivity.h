#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <utility>

#include "EndOfBookOptions.h"
#include "activities/Activity.h"

class ReaderActivity : public Activity {
 protected:
  std::string bookPath;
  int pagesUntilFullRefresh = 0;
  bool forcedRefreshPending = false;
  bool preview = false;
  static constexpr uint8_t PREVIEW_FOOTER_HEIGHT = 36;
  bool handlePreviewInput();
  void drawPreviewFooter() const;
  uint8_t readerStatusBarHeight() const;
  void readingMargins(int& top, int& right, int& bottom, int& left) const;

  std::unique_ptr<EndOfBookOptions> endOfBookOptions;
  std::atomic<bool> endOfBookOptionsReady{false};

  explicit ReaderActivity(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput,
                          std::string bookPath, bool allowFastInitialRefresh);

  virtual bool loadBook() = 0;
  virtual std::string getBookTitle() const = 0;
  virtual std::string getBookAuthor() const { return ""; }
  virtual std::string getBookThumbBmpPath() const { return ""; }

  virtual bool handleFormatInput() { return false; }
  // Lat trang THAT, moi dinh dang sach tu viet. KHONG goi thang ham nay; goi pageTurn()
  // o duoi, vi do la cho duy nhat dem so trang da lat.
  virtual bool latTrangThat(bool isForward) = 0;

  // Lat mot trang, va DEM no. Mot cho duy nhat dem, chu khong dem o ca ba trinh doc:
  // dem o ba noi thi mot dinh dang moi se im lang khong duoc dem.
  bool pageTurn(bool isForward) {
    if (!latTrangThat(isForward)) return false;
    trangDaLat++;
    return true;
  }
  virtual bool skipPages(int amount) { return pageTurn(amount > 0); }
  // Giu nut lat trang khi "Giu nut lat trang khi doc" = Co chu: doi co mot nac theo `huong`
  // (+1 to, -1 nho), KEP o hai bien. Tra ve true neu co doi. Mac dinh (XTC, bitmap) khong doi gi,
  // nhung nhip giu van bi TIEU: 0 doi co, 0 lat trang du.
  virtual bool docCoChuMotNac(int /*huong*/) { return false; }
  virtual bool isAtEndOfBook() const = 0;
  virtual void onReturnFromEndOfBook() {}

  virtual void renderBook() = 0;
  virtual void applyInitialOrientation();
  virtual void onEndOfBookRendered() {}

  bool handleBackNavigation();
  /** True while the end-of-book suggestion menu is on screen and owning input. */
  bool endOfBookMenuActive() const;
  bool handleEndOfBookMenu(bool suppressConfirmRelease = false);
  bool handleEndOfBookPageTurn(bool prevTriggered, bool nextTriggered);
  void clearEndOfBookOptionsIfNeeded();
  void disableFastInitialRefresh();

 public:
  ~ReaderActivity() override = default;
  std::string navigationMemoryKey() const override { return name + ":" + bookPath; }

  // Luot lat trang tu nguon NGOAI nut vat ly (page turner BLE). Di qua pageTurn() de van DEM
  // dung nhu nut that - goi thang latTrangThat() se bo qua bo dem trang da lat.
  bool luotLatTrangNgoai(const bool isForward) { return pageTurn(isForward); }

  static std::unique_ptr<ReaderActivity> create(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                std::string path, bool allowFastInitialRefresh, bool preview = false);

  void onEnter() override;
  void onExit() override;
  void onTick() override;
  void onPause() override;
  void onResume() override;

 protected:
  virtual bool readingPageVisible() const { return !isAtEndOfBook(); }
  bool statsEnabled = false;
  bool statsActive = false;
  bool statsDirty = false;
  std::atomic<bool> pageReady{false};
  uint32_t statsLastMs = 0;
  uint32_t statsSavedMs = 0;
  uint32_t statsDay = 0;
  uint32_t statsDayPollMs = 0;
  uint16_t trangDaLat = 0;
  void updateReadingTime(bool active);
  void chotSoLieuDoc();

 public:
  void loop() override;
  bool isPageReady() const { return pageReady.load(std::memory_order_acquire); }
  void render(RenderLock&& lock) override;

  bool isReaderActivity() const final { return !preview; }
  bool handleForcedRefresh() final;
};
