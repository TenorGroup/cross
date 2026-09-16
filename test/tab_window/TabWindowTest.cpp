#include <HalDisplay.h>
#include <HalGPIO.h>
#include <gtest/gtest.h>

#include "activities/UiListActivity.h"
#include "activities/UiTabListActivity.h"
#include "util/ButtonNavigator.h"

namespace faketest {
extern bool pressed[8];
extern bool released[8];
extern bool held[8];
void reset();
// --- kep con tro ---------------------------------------------------------------------

// Danh sach ngan di duoi chan con tro dang nho thi con tro phai ve trong bo.
TEST(KepConTro, NgoaiBoThiKeoVeDongCuoi) {
  EXPECT_EQ(UiListActivity::kepConTro(7, 5), 4);
  EXPECT_EQ(UiListActivity::kepConTro(4, 5), 4) << "dong cuoi van hop le";
  EXPECT_EQ(UiListActivity::kepConTro(0, 5), 0);
}

TEST(KepConTro, AmHoacRongThiVeKhong) {
  EXPECT_EQ(UiListActivity::kepConTro(-3, 5), 0);
  EXPECT_EQ(UiListActivity::kepConTro(2, 0), 0) << "danh sach rong thi khong co dong nao";
}

}  // namespace faketest

HalDisplay& hostTestDisplay();

namespace {

constexpr int kTabCount = 7;   // bay nhom cai dat sau khi them Thiet bi, Dong bo, Khac
constexpr int kVuaMan = 4;     // gia dinh: bon the vua man. Tang ve do con so that.

class FakeWideTabScreen final : public UiTabListActivity {
 public:
  FakeWideTabScreen(GfxRenderer& renderer, MappedInputManager& input)
      : UiTabListActivity("FakeWideTab", renderer, input) {}

  int tab = 0;

  int tabCount() const override { return kTabCount; }
  int activeTab() const override { return tab; }
  const char* tabLabel(int) const override { return "the"; }
  void onTabAction(const int index) override { tab = index; }
  void stepTab(const int direction) override {
    tab = (tab + direction + kTabCount) % kTabCount;
    veLai();
  }
  bool handleButtons() override { return false; }

  int listCount() const override { return 3; }
  void buildScreen(UiScreen&) override {}
  void activateIndex(int) override {}

  // Dung doan ma that buildTabBar() chay moi lan ve, chi thay phep do pixel bang
  // con so kVuaMan.
  void veLai() { capNhatCuaSoThe(kVuaMan); }

  int cuaSoDau_() const { return tabWindowStart(); }
  int cuaSoDai_() const { return tabWindowCount(); }
  bool theDangChonTrongTamNhin() const {
    return tab >= cuaSoDau_() && tab < cuaSoDau_() + cuaSoDai_();
  }
};

struct TabWindowFixture : public ::testing::Test {
  HalGPIO gpio;
  GfxRenderer renderer{hostTestDisplay()};
  MappedInputManager input{gpio, renderer};
  FakeWideTabScreen screen{renderer, input};

  void SetUp() override {
    ButtonNavigator::setMappedInputManager(input);
    faketest::reset();
    screen.onEnter();
    screen.veLai();  // lan ve dau tien
  }

  void bam(const uint8_t nut) {
    faketest::reset();
    faketest::pressed[nut] = true;
    screen.loop();
    faketest::reset();
    faketest::released[nut] = true;
    screen.loop();
    faketest::reset();
  }

  void nhayTheSang() { bam(HalGPIO::BTN_DOWN); }
  void nhayTheVe() { bam(HalGPIO::BTN_UP); }
};

TEST_F(TabWindowFixture, MoiLanChiHienMotCuaSoChuKhongVeHet) {
  EXPECT_EQ(screen.cuaSoDai_(), kVuaMan);
  EXPECT_LT(screen.cuaSoDai_(), kTabCount) << "ve het bay the tuc la thanh the chua chay";
}

// Di het ca vong theo chieu tien, bam nut canh THAT: the dang chon luc nao cung phai
// nam trong tam nhin.
TEST_F(TabWindowFixture, TheDangChonLuonTrongTamNhinKhiDiToi) {
  for (int i = 0; i < kTabCount * 2; i++) {
    EXPECT_TRUE(screen.theDangChonTrongTamNhin())
        << "the " << screen.tab << " nam ngoai cua so [" << screen.cuaSoDau_() << ", "
        << screen.cuaSoDau_() + screen.cuaSoDai_() << ")";
    nhayTheSang();
  }
}

// Va theo chieu lui, vi hai nut nam hai ben than may.
TEST_F(TabWindowFixture, TheDangChonLuonTrongTamNhinKhiDiLui) {
  for (int i = 0; i < kTabCount * 2; i++) {
    nhayTheVe();
    EXPECT_TRUE(screen.theDangChonTrongTamNhin())
        << "the " << screen.tab << " nam ngoai cua so [" << screen.cuaSoDau_() << ", "
        << screen.cuaSoDau_() + screen.cuaSoDai_() << ")";
  }
}

// Cua so TRUOT TUNG BUOC. The dang chon cham mep phai roi buoc tiep thi cua so dich
// dung MOT o. Nhay nguyen trang lam the dang chon vot tu mep phai ve mep trai, mat
// phai di tim lai.
TEST_F(TabWindowFixture, CuaSoTruotTungBuocChuKhongNhayTrang) {
  while (screen.tab < screen.cuaSoDau_() + screen.cuaSoDai_() - 1) nhayTheSang();
  const int dauTruoc = screen.cuaSoDau_();

  nhayTheSang();
  EXPECT_EQ(screen.cuaSoDau_(), dauTruoc + 1) << "cua so phai dich dung mot o";
}

// The dang chon chua cham mep thi cua so DUNG YEN. Cua so nhuc nhich theo tung nhip
// bam lam mat moi moc nhin.
TEST_F(TabWindowFixture, ChuaChamMepThiCuaSoDungYen) {
  const int dauTruoc = screen.cuaSoDau_();
  ASSERT_GT(screen.cuaSoDai_(), 1);
  nhayTheSang();  // tu the 0 sang the 1, con xa mep phai
  EXPECT_EQ(screen.cuaSoDau_(), dauTruoc);
}

// Quay vong: tu the cuoi bam tiep la ve the dau, va cua so nhay ve dau danh sach.
TEST_F(TabWindowFixture, TuTheCuoiBamTiepThiCuaSoVeDau) {
  for (int i = 0; i < kTabCount - 1; i++) nhayTheSang();
  ASSERT_EQ(screen.tab, kTabCount - 1);

  nhayTheSang();
  EXPECT_EQ(screen.tab, 0);
  EXPECT_EQ(screen.cuaSoDau_(), 0) << "quay vong ve the dau thi cua so phai ve dau danh sach";
}

// Chieu nguoc lai: tu the dau bam lui la ve the cuoi, cua so nhay toi cuoi danh sach.
TEST_F(TabWindowFixture, TuTheDauBamLuiThiCuaSoToiCuoi) {
  ASSERT_EQ(screen.tab, 0);
  nhayTheVe();
  EXPECT_EQ(screen.tab, kTabCount - 1);
  EXPECT_EQ(screen.cuaSoDau_(), kTabCount - kVuaMan);
}

// --- phep chia cua so, goi thang ---------------------------------------------------

TEST(TinhCuaSo, ItTheHonSucChuaThiVeHet) {
  const auto ra = UiTabListActivity::tinhCuaSo(4, 0, 0, 5);
  EXPECT_EQ(ra.dau, 0);
  EXPECT_EQ(ra.dai, 4);
}

TEST(TinhCuaSo, KhongBaoGioTroRaNgoaiDanhSach) {
  for (int tong = 1; tong <= 9; tong++) {
    for (int chon = 0; chon < tong; chon++) {
      for (int dauCu = -2; dauCu <= tong + 2; dauCu++) {
        const auto ra = UiTabListActivity::tinhCuaSo(tong, chon, dauCu, 4);
        EXPECT_GE(ra.dau, 0);
        EXPECT_GE(ra.dai, 1);
        EXPECT_LE(ra.dau + ra.dai, tong) << "cua so tro ra ngoai danh sach";
        EXPECT_GE(chon, ra.dau) << "the dang chon nam truoc cua so";
        EXPECT_LT(chon, ra.dau + ra.dai) << "the dang chon nam sau cua so";
      }
    }
  }
}

// --- phep do so the vua man ---------------------------------------------------------

// Tran la nam, san la bon. Nhan rong qua thi giu san bon va di sua TEN, cam giam the.
TEST(TheVuaMan, TranNamSanBon) {
  EXPECT_EQ(UiTabListActivity::theVuaMan(528, 20, 12, 0, 9), 5) << "nhan ngan thi lay tran nam";
  EXPECT_EQ(UiTabListActivity::theVuaMan(528, 120, 12, 0, 9), 4) << "nhan rong thi tut ve bon";
  EXPECT_EQ(UiTabListActivity::theVuaMan(528, 400, 12, 0, 9), 4) << "nhan rong qua van giu san bon";
}

TEST(TheVuaMan, KhongVuotTongSoThe) {
  EXPECT_EQ(UiTabListActivity::theVuaMan(528, 20, 12, 0, 3), 3);
  EXPECT_EQ(UiTabListActivity::theVuaMan(528, 400, 12, 0, 2), 2);
}

// Khoang ho giua cac o an mat be rong that. Do that 14/09 tren menu doc: nhan "Yeu thich"
// rong 93 px, vien 8 px, khoang ho 8 px. Bo qua khoang ho thi o o muc nam the la 105 px
// va phep tinh noi la vua; tinh ca khoang ho thi o chi con 99 px, nen phai tut ve bon.
TEST(TheVuaMan, KhoangHoGiuaCacOCungAnBeRong) {
  EXPECT_EQ(UiTabListActivity::theVuaMan(528, 93, 8, 0, 5), 5) << "khong ho thi nam the vua";
  EXPECT_EQ(UiTabListActivity::theVuaMan(528, 93, 8, 8, 5), 4) << "co ho thi nam the khong vua nua";
}

}  // namespace
