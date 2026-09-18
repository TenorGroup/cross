// BTH2: bo kiem duong lat trang cua page-turner BLE bang dau vao GIA.
//
// Ma duoc bien dich o day la ma SAN XUAT, khong phai ban chep:
//   * freeink::BleKeyboardHost (src/BleKeyboardHost.cpp + src/HidKeymap.cpp) voi
//     FREEINK_CAP_BLE_HID_HOST=1, dieu khien bang fake NimBLE cua chinh thu vien
//     (tests/FakeBle.*). Dau vao bom vao la KHUNG REPORT HID THAT (8 byte, boot
//     keyboard: byte 0 modifier, byte 2..7 usage) gui qua notify() - dung hinh
//     dang ma mot remote page-turner gui. Khong co KeyEvent nao duoc tu tao.
//   * CrossPointSettings::blePageActionFor (src/CrossPointSettings.h), dung loi goi
//     ma src/main.cpp dung cho tung su kien lay ra khoi hang doi.
//
// DO DUOC: voi moi chuoi bom vao, bao nhieu luot lat trang duoc QUYET DINH va theo
// huong nao (so lieu nam trong bao cao ban giao).
//
// GIOI HAN - doc truoc khi tin vao con so: bon luat sau nam trong khoi
// `#if CROSSPOINT_BLE_HID_HOST` cua src/main.cpp (dong 687-741) va KHONG duoc bo
// kiem nay phu: (1) toi da MOT luot lat cho moi vong lap, (2) mat ket noi host thi
// xoa luot cho, (3) roi trinh doc thi xoa luot cho + rong hang doi, (4) host khong
// chay thi khong phat luot nao; cong them viec tam dung khi truyen tep va khoi phuc
// co cong RAM 32.768 B. Bo kiem nay do o tang duoi: so SU KIEN ma host giao cho
// ung dung, va so HANH DONG (Next/Previous) ma anh xa that quyet dinh. Mot chuoi ma
// main.cpp rut het trong mot vong lap thi o day dem theo tung su kien, co y nhu vay:
// chinh main.cpp moi la noi gop chung lai thanh mot luot.

#include <gtest/gtest.h>

#include <cstdint>
#include <initializer_list>
#include <vector>

#include "BleKeyboardHost.h"
#include "CrossPointSettings.h"
#include "activities/settings/BleKeyBinding.h"
#include "FakeBle.h"
#include "HidDescriptors.h"
#include "HidKeymap.h"

using namespace freeink;  // test-only: the SDK's namespace

namespace {

constexpr char kAddr[] = "AA:BB:CC:DD:EE:FF";  // the remote that pairs and connects

// HID service characteristics the host discovers (HID 1.11 section 3.4).
enum : uint16_t {
  kUuidReportMap = 0x2A4B,
  kUuidReport = 0x2A4D,
  kUuidProtocolMode = 0x2A4E,
};

// Usages a page-turner remote can send (HID Usage Tables, Keyboard/Keypad page).
constexpr uint8_t kUsageA = 0x04;
constexpr uint8_t kUsageB = 0x05;
constexpr uint8_t kUsageEnter = 0x28;
constexpr uint8_t kUsageBackspace = 0x2A;
constexpr uint8_t kUsageSpace = 0x2C;
constexpr uint8_t kUsagePageUp = 0x4B;
constexpr uint8_t kUsagePageDown = 0x4E;
constexpr uint8_t kUsageRight = 0x4F;
constexpr uint8_t kUsageLeft = 0x50;
constexpr uint8_t kUsageDown = 0x51;
constexpr uint8_t kUsageUp = 0x52;
constexpr uint8_t kUsageScanPrev = 0xB6;
constexpr uint8_t kUsageScanNext = 0xB5;
constexpr uint8_t kUsageVolumeDown = 0xEA;
constexpr uint8_t kUsageVolumeUp = 0xE9;

// One 8-byte boot-shaped report: modifier byte, reserved byte, six key slots.
std::vector<uint8_t> bootReport(uint8_t mods, std::initializer_list<uint8_t> keys) {
  std::vector<uint8_t> report(8, 0);
  report[0] = mods;
  uint8_t slot = 0;
  for (uint8_t key : keys) {
    if (slot >= 6) break;
    report[2 + slot++] = key;
  }
  return report;
}

// What the page-turner path decided for the events it was handed. `events` is the
// number of key events the host made available to the app, `previous`/`next` the
// page turns those events map to.
struct Turns {
  int events = 0;
  int previous = 0;
  int next = 0;
  uint8_t lastUsage = 0;  // ma THO cua su kien cuoi - cung la ma in ra log chan doan
  int total() const { return previous + next; }
};

class PageTurnerFakeInputTest : public ::testing::Test {
 protected:
  void SetUp() override {
    fakeble::resetWorld();
    savedEnabled_ = SETTINGS.blePageTurnerEnabled;
    savedPrev_ = SETTINGS.blePrevKeyUsage;
    savedNext_ = SETTINGS.bleNextKeyUsage;
    // The user turned the page turner on, and (unless a test says otherwise) no key
    // has been learned yet, so the four default usages are live.
    SETTINGS.blePageTurnerEnabled = 1;
    SETTINGS.blePrevKeyUsage = 0;
    SETTINGS.bleNextKeyUsage = 0;
  }

  void TearDown() override {
    fakeble::endHost();
    SETTINGS.blePageTurnerEnabled = savedEnabled_;
    SETTINGS.blePrevKeyUsage = savedPrev_;
    SETTINGS.bleNextKeyUsage = savedNext_;
  }

  // Pair and connect a fake remote that serves the boot keyboard report map.
  void connectRemote() {
    const int map = fakeble::addCharacteristic(kUuidReportMap, /*canRead=*/true);
    fakeble::setCharacteristicValue(map, hidtest::kBootKeyboard, sizeof hidtest::kBootKeyboard);
    const int protocol = fakeble::addCharacteristic(kUuidProtocolMode, false, /*canWrite=*/true);
    reportChar_ = fakeble::addCharacteristic(kUuidReport, false, false, /*canNotify=*/true);

    ASSERT_TRUE(fakeble::beginHost());
    ASSERT_EQ(fakeble::connectTo(kAddr), fakeble::LinkResult::Connected);

    // Report Protocol (1) is requested before subscribing, like a desktop host; a
    // not-subscribed characteristic would silently swallow every press below.
    const uint8_t reportProtocol[1] = {1};
    ASSERT_TRUE(fakeble::writeWasSent(protocol, reportProtocol, 1));
    ASSERT_TRUE(fakeble::isSubscribed(reportChar_));
  }

  // One press frame from the remote.
  void press(uint8_t usage, uint8_t mods = 0) {
    const std::vector<uint8_t> frame = bootReport(mods, {usage});
    ASSERT_TRUE(fakeble::notify(reportChar_, frame.data(), frame.size()));
  }

  // A remote shaped like a cheap accessory: two report layouts, where report id 2 is
  // a 16-bit Consumer array - the shape a volume key arrives in. The characteristic
  // is wired by Report Reference (Report id 2, type 1 = Input), the same way a real
  // device declares it.
  void connectConsumerRemote() {
    const int map = fakeble::addCharacteristic(kUuidReportMap, /*canRead=*/true);
    fakeble::setCharacteristicValue(map, hidtest::kTwoReports, sizeof hidtest::kTwoReports);
    const int protocol = fakeble::addCharacteristic(kUuidProtocolMode, false, /*canWrite=*/true);
    const uint8_t refConsumer[2] = {2, 1};
    consumerChar_ = fakeble::addCharacteristic(kUuidReport, false, false, /*canNotify=*/true);
    fakeble::setReportReference(consumerChar_, refConsumer, sizeof refConsumer);

    ASSERT_TRUE(fakeble::beginHost());
    ASSERT_EQ(fakeble::connectTo(kAddr), fakeble::LinkResult::Connected);
    const uint8_t reportProtocol[1] = {1};
    ASSERT_TRUE(fakeble::writeWasSent(protocol, reportProtocol, 1));
    ASSERT_TRUE(fakeble::isSubscribed(consumerChar_));
  }

  // One Consumer-page press (16-bit usage, little endian).
  void pressConsumer(uint16_t usage) {
    const uint8_t frame[3] = {2, static_cast<uint8_t>(usage & 0xFF), static_cast<uint8_t>(usage >> 8)};
    ASSERT_TRUE(fakeble::notify(consumerChar_, frame, sizeof frame));
  }

  // The frame that follows a released Consumer button.
  void releaseConsumer() {
    const uint8_t frame[3] = {2, 0, 0};
    ASSERT_TRUE(fakeble::notify(consumerChar_, frame, sizeof frame));
  }

  // The frame that follows a released button: no usage, so no new event.
  void releaseAll() {
    const std::vector<uint8_t> frame = bootReport(0, {});
    ASSERT_TRUE(fakeble::notify(reportChar_, frame.data(), frame.size()));
  }

  // Bleed the queue dry exactly like the app does and count what the real mapping
  // decides for each event.
  Turns drainTurns() {
    Turns turns;
    KeyEvent ev;
    while (fakeble::host().popKey(ev)) {
      ++turns.events;
      turns.lastUsage = ev.keycode;
      switch (SETTINGS.blePageActionFor(ev.keycode, ev.mods)) {
        case CrossPointSettings::BlePageAction::PreviousPage:
          ++turns.previous;
          break;
        case CrossPointSettings::BlePageAction::NextPage:
          ++turns.next;
          break;
        case CrossPointSettings::BlePageAction::None:
          break;
      }
    }
    return turns;
  }

 private:
  int reportChar_ = -1;
  int consumerChar_ = -1;
  uint8_t savedEnabled_ = 0;
  uint8_t savedPrev_ = 0;
  uint8_t savedNext_ = 0;
};

// --- Mot phim da gan -> dung MOT luot, dung huong ----------------------------

TEST_F(PageTurnerFakeInputTest, AssignedKeyTurnsExactlyOnePageInItsDirection) {
  connectRemote();

  press(kUsageRight);
  releaseAll();
  const Turns nextTurn = drainTurns();
  EXPECT_EQ(nextTurn.events, 1);
  EXPECT_EQ(nextTurn.next, 1);
  EXPECT_EQ(nextTurn.previous, 0);

  press(kUsageLeft);
  releaseAll();
  const Turns prevTurn = drainTurns();
  EXPECT_EQ(prevTurn.events, 1);
  EXPECT_EQ(prevTurn.previous, 1);
  EXPECT_EQ(prevTurn.next, 0);

  // A release frame on its own is not a press: no event, so no turn.
  releaseAll();
  EXPECT_EQ(drainTurns().total(), 0);
}

TEST_F(PageTurnerFakeInputTest, PageUpAndPageDownAreAssignedByDefault) {
  connectRemote();

  press(kUsagePageUp);
  releaseAll();
  EXPECT_EQ(drainTurns().previous, 1);

  press(kUsagePageDown);
  releaseAll();
  EXPECT_EQ(drainTurns().next, 1);
}

// --- Su kien kem modifier bi bo ---------------------------------------------

TEST_F(PageTurnerFakeInputTest, ModifierPressesReachTheAppButTurnNoPage) {
  connectRemote();

  press(kUsageRight, HID_LCTRL);
  releaseAll();
  press(kUsagePageDown, HID_LSHIFT);
  releaseAll();
  press(kUsageLeft, HID_LALT);
  releaseAll();

  const Turns withModifiers = drainTurns();
  EXPECT_EQ(withModifiers.events, 3) << "the events never reached the app";
  EXPECT_EQ(withModifiers.total(), 0) << "a modifier press turned a page";

  // Control: the same usage without a modifier does turn, so the zero above is the
  // modifier being filtered and not a dead path.
  press(kUsageRight);
  releaseAll();
  EXPECT_EQ(drainTurns().next, 1);
}

// --- Cong tac tat ------------------------------------------------------------

TEST_F(PageTurnerFakeInputTest, DisabledPageTurnerTurnsNothing) {
  SETTINGS.blePageTurnerEnabled = 0;
  connectRemote();

  press(kUsageRight);
  releaseAll();
  press(kUsageLeft);
  releaseAll();

  const Turns turns = drainTurns();
  EXPECT_EQ(turns.events, 2) << "the events never reached the app";
  EXPECT_EQ(turns.total(), 0) << "a page turned while the setting was off";
}

// --- Nut da hoc thay cho mac dinh cua dung huong do --------------------------

TEST_F(PageTurnerFakeInputTest, LearnedKeyReplacesTheDefaultForThatDirection) {
  SETTINGS.bleNextKeyUsage = kUsageEnter;  // a remote whose Next button is Enter
  connectRemote();

  press(kUsageEnter);
  releaseAll();
  EXPECT_EQ(drainTurns().next, 1) << "the learned key did not turn the page";

  press(kUsageRight);
  releaseAll();
  EXPECT_EQ(drainTurns().total(), 0) << "the replaced default still turned a page";

  // Nothing was learned for the other direction, so its defaults stay live.
  press(kUsagePageUp);
  releaseAll();
  EXPECT_EQ(drainTurns().previous, 1);
}

// --- Phim chua gan -----------------------------------------------------------

TEST_F(PageTurnerFakeInputTest, UnassignedKeysTurnNoPage) {
  connectRemote();

  // Enter used to stand here; it is a default Next usage now (see
  // CheapRemoteDefaultsTurnPagesWithoutLearning), so the letter keys carry the
  // "typing must not flip pages" contract instead.
  press(kUsageA);
  releaseAll();
  press(kUsageB);
  releaseAll();

  const Turns turns = drainTurns();
  EXPECT_EQ(turns.events, 2);
  EXPECT_EQ(turns.total(), 0);
}

// --- So lieu tong: mot chuoi tron -------------------------------------------------

TEST_F(PageTurnerFakeInputTest, MixedScriptTurnsExactlyTheAssignedPages) {
  connectRemote();

  press(kUsageRight);
  releaseAll();
  press(kUsagePageDown);
  releaseAll();
  press(kUsageRight);
  releaseAll();
  press(kUsageRight);
  releaseAll();
  press(kUsagePageUp);
  releaseAll();
  press(kUsageLeft);
  releaseAll();

  press(kUsageRight, HID_LCTRL);  // Ctrl+Right: ignored
  releaseAll();
  press(kUsageLeft, HID_LSHIFT);  // Shift+Left: ignored
  releaseAll();

  press(kUsageRight);  // a held button repeating its frame, then the release
  press(kUsageRight);
  releaseAll();

  press(kUsageA);  // unassigned
  releaseAll();

  const Turns turns = drainTurns();
  // Five Next (four assigned presses + the held button's single fresh press) and two
  // Previous, out of ten events the host handed over.
  EXPECT_EQ(turns.next, 5);
  EXPECT_EQ(turns.previous, 2);
  EXPECT_EQ(turns.total(), 7);
  EXPECT_EQ(turns.events, 10);
}

// --- Nhieu su kien trong mot lan rut ----------------------------------------

TEST_F(PageTurnerFakeInputTest, QueuedPressesAreDecidedPerEventNotCollapsed) {
  connectRemote();

  // Three presses arrive before the app drains anything - one loop's worth of
  // backlog. Read the file header: this layer hands the ingest three separate
  // decisions; the collapse to at most ONE turn per loop iteration is main.cpp's
  // and is deliberately not asserted here.
  press(kUsageRight);
  releaseAll();
  press(kUsageRight);
  releaseAll();
  press(kUsageRight);
  releaseAll();

  const Turns turns = drainTurns();
  EXPECT_EQ(turns.events, 3);
  EXPECT_EQ(turns.next, 3);
  EXPECT_EQ(turns.previous, 0);
}

// --- Mac dinh rong hon: dieu khien gia khong phai hoc nut --------------------

TEST_F(PageTurnerFakeInputTest, CheapRemoteDefaultsTurnPagesWithoutLearning) {
  connectRemote();

  const uint8_t nextDefaults[] = {kUsageDown, kUsageSpace, kUsageEnter, kUsageVolumeUp, kUsageScanNext};
  for (const uint8_t usage : nextDefaults) {
    press(usage);
    releaseAll();
    EXPECT_EQ(drainTurns().next, 1)
        << "usage 0x" << std::hex << static_cast<int>(usage) << " did not turn forward by default";
  }

  const uint8_t prevDefaults[] = {kUsageUp, kUsageBackspace, kUsageVolumeDown, kUsageScanPrev};
  for (const uint8_t usage : prevDefaults) {
    press(usage);
    releaseAll();
    EXPECT_EQ(drainTurns().previous, 1)
        << "usage 0x" << std::hex << static_cast<int>(usage) << " did not turn back by default";
  }

  // Typing still must not flip pages.
  press(kUsageA);
  releaseAll();
  EXPECT_EQ(drainTurns().total(), 0);
}

TEST_F(PageTurnerFakeInputTest, LearnedKeyRetiresTheNewDefaultForItsDirection) {
  SETTINGS.bleNextKeyUsage = kUsageVolumeUp;  // a remote whose Next button is Volume Up
  connectRemote();

  press(kUsageVolumeUp);
  releaseAll();
  EXPECT_EQ(drainTurns().next, 1) << "the learned key did not turn the page";

  press(kUsageDown);
  releaseAll();
  EXPECT_EQ(drainTurns().total(), 0) << "the replaced default still turned a page";
}

TEST_F(PageTurnerFakeInputTest, ModifiersOnTheNewDefaultsTurnNoPage) {
  connectRemote();

  press(kUsageDown, HID_LCTRL);
  releaseAll();
  press(kUsageVolumeUp, HID_LSHIFT);
  releaseAll();
  const Turns withModifiers = drainTurns();
  EXPECT_EQ(withModifiers.events, 2) << "the events never reached the app";
  EXPECT_EQ(withModifiers.total(), 0) << "a modifier press turned a page";

  press(kUsageDown);  // control: the same usage alone turns
  releaseAll();
  EXPECT_EQ(drainTurns().next, 1);
}

// --- Remote gui trang Consumer: ma phai toi noi ------------------------------

TEST_F(PageTurnerFakeInputTest, ConsumerVolumeRemoteIsDecodedAndTurnsThePage) {
  connectConsumerRemote();

  pressConsumer(0x00E9);
  releaseConsumer();
  const Turns up = drainTurns();
  ASSERT_EQ(up.events, 1) << "the consumer report never reached the app";
  EXPECT_EQ(up.lastUsage, kUsageVolumeUp) << "consumer 0x00E9 must arrive as its low byte 0xE9";
  EXPECT_EQ(up.next, 1);

  pressConsumer(0x00EA);
  releaseConsumer();
  EXPECT_EQ(drainTurns().previous, 1);
}

// --- Vong hoc nut: phim gia -> luat gan -> anh xa ----------------------------

TEST_F(PageTurnerFakeInputTest, BindingLoopLearnsTheFirstUsageTheRemoteSends) {
  connectRemote();

  KeyEvent ev;
  int accepted = 0;
  const auto learnNext = [&] {
    while (fakeble::host().popKey(ev)) {
      if (blebinding::assign(blebinding::Direction::Next, ev.keycode, ev.mods)) ++accepted;
    }
  };

  // A release frame arrives first: usage 0 must not be learned as a button.
  releaseAll();
  learnNext();
  EXPECT_EQ(accepted, 0) << "a release frame was learned as a key";
  EXPECT_EQ(SETTINGS.bleNextKeyUsage, 0);

  // The next real press is the one that gets bound.
  press(kUsageDown);
  releaseAll();
  learnNext();
  EXPECT_EQ(accepted, 1);
  EXPECT_EQ(SETTINGS.bleNextKeyUsage, kUsageDown);
  EXPECT_EQ(blebinding::assigned(blebinding::Direction::Next), kUsageDown);
  EXPECT_EQ(blebinding::assigned(blebinding::Direction::Prev), 0) << "the other direction was touched";

  // The learned key turns the page, and the defaults it replaced go quiet.
  press(kUsageDown);
  releaseAll();
  EXPECT_EQ(drainTurns().next, 1);
  press(kUsageRight);
  releaseAll();
  EXPECT_EQ(drainTurns().total(), 0) << "a retired default still turned a page";

  // Clearing the binding brings the defaults back.
  blebinding::clear(blebinding::Direction::Next);
  EXPECT_EQ(blebinding::assigned(blebinding::Direction::Next), 0);
  press(kUsageDown);
  releaseAll();
  EXPECT_EQ(drainTurns().next, 1) << "the default was not live again after clearing";
}

}  // namespace
