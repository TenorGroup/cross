#include <gtest/gtest.h>

#include <map>
#include <vector>

#include "activities/settings/SettingsTabs.h"

namespace {

using settingstabs::Action;
using settingstabs::Tab;

// Moi dong hanh dong dang co tren may. None khong phai mot dong.
const std::vector<Action> kDongHanhDong = {
    Action::RemapFrontButtons, Action::CustomiseStatusBar, Action::KOReaderSync,    Action::OPDSBrowser,
    Action::Network,           Action::ClearCache,         Action::CheckForUpdates, Action::SdFirmwareUpdate,
    Action::Language,          Action::DownloadFonts,      Action::TextSettings,    Action::KeyboardLayouts,
    Action::DeviceName,        Action::FileTransfer,       Action::BrowseOPDS,
};

std::map<Tab, int> demTheoThe() {
  std::map<Tab, int> dem;
  for (const auto action : kDongHanhDong) dem[settingstabs::nhaCua(action)]++;
  return dem;
}

TEST(SettingsTabs, CuaDongHoNamTrongHeThong) { EXPECT_EQ(settingstabs::nhaCua(Action::Clock), Tab::SYSTEM); }

TEST(SettingsTabs, BayTheCoBanPhim) {
  EXPECT_EQ(settingstabs::TAB_COUNT, 7);
  EXPECT_EQ(settingstabs::nhaCua(Action::KeyboardLayouts), Tab::KEYBOARD);
}

TEST(SettingsTabs, TheNaoCungCoNhan) {
  for (int i = 0; i < settingstabs::TAB_COUNT; i++) {
    EXPECT_NE(settingstabs::tenThe(static_cast<Tab>(i)), StrId::STR_NONE_OPT);
  }
  // Bay nhan phai KHAC NHAU: hai the cung ten la nguoi dung khong biet minh dang o dau.
  std::vector<StrId> nhan;
  for (int i = 0; i < settingstabs::TAB_COUNT; i++) nhan.push_back(settingstabs::tenThe(static_cast<Tab>(i)));
  for (size_t a = 0; a < nhan.size(); a++) {
    for (size_t b = a + 1; b < nhan.size(); b++) {
      EXPECT_NE(nhan[a], nhan[b]) << "hai the mang cung mot nhan";
    }
  }
}

TEST(SettingsTabs, TheHeThongKhongOmDongHanhDongNao) {
  for (const auto action : kDongHanhDong) {
    EXPECT_NE(settingstabs::nhaCua(action), Tab::SYSTEM) << "con mot dong hanh dong con nam trong the He thong";
  }
}

// Hai the moi phai co dong that. Mot the rong la mot nhip bam vut di.
TEST(SettingsTabs, HaiTheMoiDeuCoDong) {
  const auto dem = demTheoThe();
  EXPECT_GT(dem.count(Tab::DEVICE) ? dem.at(Tab::DEVICE) : 0, 0) << "the Thiet bi rong";
  EXPECT_GT(dem.count(Tab::OTHER) ? dem.at(Tab::OTHER) : 0, 0) << "the Khac rong";
}

TEST(SettingsTabs, KhongTheNaoQuaTranBayDong) {
  for (const auto& [tab, so] : demTheoThe()) {
    EXPECT_LE(so, 7) << "mot the om qua bay dong hanh dong";
  }
}

// Moi dong co dung mot nha, va nha do phai la mot the co that.
TEST(SettingsTabs, MoiDongCoDungMotNhaCoThat) {
  for (const auto action : kDongHanhDong) {
    const int tab = static_cast<int>(settingstabs::nhaCua(action));
    EXPECT_GE(tab, 0);
    EXPECT_LT(tab, settingstabs::TAB_COUNT);
  }
}

// --- nguong mo trinh chon ---------------------------------------------------------

TEST(NguongTrinhChon, TuBonTroLenMoiMo) {
  EXPECT_FALSE(settingstabs::moTrinhChon(2)) << "hai lua chon thi bam mot nhip la xong";
  EXPECT_FALSE(settingstabs::moTrinhChon(3)) << "ba lua chon van doi tai cho";
  EXPECT_TRUE(settingstabs::moTrinhChon(4)) << "four choices open the picker";
  EXPECT_TRUE(settingstabs::moTrinhChon(6));
}

// Danh sach rong hay mot lua chon thi khong co gi de mo.
TEST(NguongTrinhChon, ItHonHaiThiKhongMo) {
  EXPECT_FALSE(settingstabs::moTrinhChon(0));
  EXPECT_FALSE(settingstabs::moTrinhChon(1));
}

// --- the Cai dat o man chinh ---------------------------------------------------------

TEST(TheCaiDatManChinh, BayDuMoiNhomChuKhongPhaiMotDong) {
  StrId dong[16];
  const int n = settingstabs::dongCuaTheCaiDat(dong, 16);
  EXPECT_EQ(n, settingstabs::TAB_COUNT);
  EXPECT_GT(n, 1) << "mot dong tuc la lai bat nguoi ta bam them mot nhip vo nghia";
  EXPECT_NE(dong[0], StrId::STR_SETTINGS_TITLE) << "dong dau van la chu Cai dat";
}

TEST(TheCaiDatManChinh, KhongDongNaoTrungNhau) {
  StrId dong[16];
  const int n = settingstabs::dongCuaTheCaiDat(dong, 16);
  for (int a = 0; a < n; a++) {
    for (int b = a + 1; b < n; b++) EXPECT_NE(dong[a], dong[b]);
  }
}

TEST(TheCaiDatManChinh, KhongTranKhiChoItCho) {
  StrId dong[3];
  EXPECT_EQ(settingstabs::dongCuaTheCaiDat(dong, 3), 3);
}

}  // namespace

TEST(SettingsTabs, OtherIsAlwaysLast) {
  StrId labels[settingstabs::TAB_COUNT];
  const int count = settingstabs::dongCuaTheCaiDat(labels, settingstabs::TAB_COUNT);
  EXPECT_EQ(labels[count - 1], StrId::STR_CAT_OTHER);
}
