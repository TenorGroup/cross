// BTH2: bo kiem duong lat trang cua page-turner BLE bang dau vao GIA.
//
// Ma duoc bien dich o day la ma SAN XUAT, khong phai ban chep:
//   * freeink::BleKeyboardHost (src/BleKeyboardHost.cpp + src/HidKeymap.cpp) voi
//     FREEINK_CAP_BLE_HID_HOST=1, dieu khien bang fake NimBLE cua chinh thu vien
//     (tests/FakeBle.*). Dau vao bom vao la KHUNG REPORT HID THAT (8 byte, boot
//     keyboard: byte 0 modifier, byte 2..7 usage) gui qua notify() - dung hinh
//     dang ma mot remote page-turner gui. Khong co KeyEvent nao duoc tu tao.
//   * bleturner::pageActionFor (lib/BlePageTurner), dung loi goi ma module lat trang
//     dung cho tung su kien lay ra khoi hang doi.
//
// DO DUOC: voi moi chuoi bom vao, bao nhieu luot lat trang duoc QUYET DINH va theo
// huong nao (so lieu nam trong bao cao ban giao).
//
// GIOI HAN - doc truoc khi tin vao con so: bon luat sau nam trong tick() cua
// lib/BlePageTurner (kiem rieng o TickTest cua module) va KHONG duoc bo kiem nay phu: (1) toi da MOT luot lat cho moi vong lap, (2) mat ket noi host thi
// xoa luot cho, (3) roi trinh doc thi xoa luot cho + rong hang doi, (4) host khong
// chay thi khong phat luot nao; cong them viec tam dung khi truyen tep va khoi phuc
// co cong RAM 32.768 B. Bo kiem nay do o tang duoi: so SU KIEN ma host giao cho
// ung dung, va so HANH DONG (Next/Previous) ma anh xa that quyet dinh. Mot chuoi ma
// module rut het trong mot vong lap thi o day dem theo tung su kien, co y nhu vay:
// chinh tick() moi la noi gop chung lai thanh mot luot.

#include <gtest/gtest.h>

#include <cstdint>
#include <initializer_list>
#include <vector>

#include "BleKeyboardHost.h"
#include "BlePageTurner.h"
#include "FakeBle.h"
#include "HidDescriptors.h"
#include "HidKeymap.h"

using namespace freeink;  // test-only: the SDK's namespace

namespace {

constexpr char kAddr[] = "AA:BB:CC:DD:EE:FF";  // the remote that pairs and connects

// The page turner's saved settings, as the host holds them.
bleturner::Config config;

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

// What the page-turner path decided for the events it was handed. The host hands the
// app BOTH edges of a button, so a direction is decided once, on the press edge, and
// `releases` counts the matching second halves. `events` is the number of press
// events the host made available, `previous`/`next` the page turns they map to.
struct Turns {
  int events = 0;
  int releases = 0;
  int previous = 0;
  int next = 0;
  uint8_t lastUsage = 0;  // ma THO cua su kien cuoi - cung la ma in ra log chan doan
  int total() const { return previous + next; }
};

class PageTurnerFakeInputTest : public ::testing::Test {
 protected:
  void SetUp() override {
    fakeble::resetWorld();
    // The user turned the page turner on, and (unless a test says otherwise) no key
    // has been learned yet, so the four default usages are live.
    config = bleturner::Config{};
    config.enabled = 1;
  }

  void TearDown() override {
    fakeble::endHost();
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
      if (!ev.pressed) {
        ++turns.releases;
        continue;  // the release edge repeats the button's identity, it decides nothing
      }
      ++turns.events;
      turns.lastUsage = ev.keycode;
      switch (bleturner::pageActionFor(config, ev.keycode, ev.mods)) {
        case bleturner::Action::PrevPage:
          ++turns.previous;
          break;
        case bleturner::Action::NextPage:
          ++turns.next;
          break;
        default:
          break;
      }
    }
    return turns;
  }

 private:
  int reportChar_ = -1;
  int consumerChar_ = -1;
};

// --- Mot phim da gan -> dung MOT luot, dung huong ----------------------------

TEST_F(PageTurnerFakeInputTest, AssignedKeyTurnsExactlyOnePageInItsDirection) {
  connectRemote();

  press(kUsageRight);
  releaseAll();
  const Turns nextTurn = drainTurns();
  EXPECT_EQ(nextTurn.events, 1);
  EXPECT_EQ(nextTurn.releases, 1) << "the press edge arrived without its release";
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
  config.enabled = 0;
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
  config.nextKeyUsage = kUsageEnter;  // a remote whose Next button is Enter
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

  // Each gesture is drained where it happens, which is what the main loop does on
  // every iteration: ten gestures held unread would be twenty events, past the ring.
  Turns turns;
  const auto drainInto = [&] {
    const Turns batch = drainTurns();
    turns.events += batch.events;
    turns.releases += batch.releases;
    turns.previous += batch.previous;
    turns.next += batch.next;
  };
  const auto gesture = [&](uint8_t usage, uint8_t mods = 0) {
    press(usage, mods);
    drainInto();
    releaseAll();
    drainInto();
  };

  gesture(kUsageRight);
  gesture(kUsagePageDown);
  gesture(kUsageRight);
  gesture(kUsageRight);
  gesture(kUsagePageUp);
  gesture(kUsageLeft);

  gesture(kUsageRight, HID_LCTRL);  // Ctrl+Right: ignored
  gesture(kUsageLeft, HID_LSHIFT);  // Shift+Left: ignored

  press(kUsageRight);  // a held button repeating its frame, then the release
  drainInto();
  press(kUsageRight);
  drainInto();
  releaseAll();
  drainInto();

  gesture(kUsageA);  // unassigned

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
  config.nextKeyUsage = kUsageVolumeUp;  // a remote whose Next button is Volume Up
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

// --- Per-button binding from RAW frames ----------------------------------------
//
// The same chain the module's tick runs for the connected remote: pick the table by
// address and name, drain raw edges, let bleturner::onRawEdge decide (falling back
// to the old usage mapping for a button the table does not name), drain key events
// and IGNORE them when there is a table.

// What the routed path decided, counted per action.
struct Routed {
  int next = 0;
  int previous = 0;
  int nextChapter = 0;
  int previousChapter = 0;
  int total() const { return next + previous + nextChapter + previousChapter; }
};

class RemoteBindingTest : public PageTurnerFakeInputTest {
 protected:
  void SetUp() override {
    PageTurnerFakeInputTest::SetUp();
    router_ = bleturner::Router();
  }

  // The three-button remote (map rebuilt from the device log), advertised under
  // `name` so the host resolves it on link-up the way it does for a real scan.
  void connectThreeButton(const char* name) {
    const int map = fakeble::addCharacteristic(kUuidReportMap, /*canRead=*/true);
    fakeble::setCharacteristicValue(map, hidtest::kThreeButtonRemote, sizeof hidtest::kThreeButtonRemote);
    fakeble::addCharacteristic(kUuidProtocolMode, false, /*canWrite=*/true);
    const uint8_t ref3[2] = {3, 1};
    media_ = fakeble::addCharacteristic(kUuidReport, false, false, /*canNotify=*/true);
    fakeble::setReportReference(media_, ref3, sizeof ref3);
    ASSERT_TRUE(fakeble::beginHost());
    fakeble::host().startScan();
    fakeble::state().advertise(kAddr, name, -40);
    ASSERT_EQ(fakeble::connectTo(kAddr), fakeble::LinkResult::Connected);
    ASSERT_STREQ(fakeble::host().connectedName(), name);
  }

  void frame(std::initializer_list<uint8_t> bytes) {
    const std::vector<uint8_t> data(bytes);
    const size_t before = fakeble::allocationCount();
    ASSERT_TRUE(fakeble::notify(media_, data.data(), data.size()));
    EXPECT_EQ(fakeble::allocationCount(), before) << "notification path allocated";
  }

  void count(Routed& r, const bleturner::Action a) {
    switch (a) {
      case bleturner::Action::NextPage: ++r.next; break;
      case bleturner::Action::PrevPage: ++r.previous; break;
      case bleturner::Action::NextChapter: ++r.nextChapter; break;
      case bleturner::Action::PrevChapter: ++r.previousChapter; break;
      default: break;
    }
  }

  // One main-loop pass over everything the host queued, as the module's tick runs it.
  Routed route(const uint32_t nowMs) {
    Routed r;
    router_.follow(fakeble::host().isConnected());
    if (router_.linked && !router_.chosen) {
      router_.table = bleturner::tableFor(config.remotes, config.remoteCount,
                                           fakeble::host().connectedAddr(), fakeble::host().connectedName());
      router_.chosen = true;
    }
    const bool viaTable = bleturner::routes(router_.table);
    RawButtonEvent raw;
    while (fakeble::host().popRawButton(raw)) {
      if (!viaTable) continue;
      count(r, bleturner::onRawEdge(*router_.table, raw.code(), raw.pressed, raw.atMs,
                                    bleturner::pageActionFor(config, raw.keycode, raw.mods), router_.wait));
    }
    if (viaTable) count(r, bleturner::pollHold(router_.wait, nowMs));
    KeyEvent ev;
    while (fakeble::host().popKey(ev)) {
      if (viaTable || !ev.pressed) continue;
      const auto a = bleturner::pageActionFor(config, ev.keycode, ev.mods);
      if (a == bleturner::Action::NextPage) ++r.next;
      if (a == bleturner::Action::PrevPage) ++r.previous;
    }
    return r;
  }

  Routed tap(std::initializer_list<uint8_t> press) {
    frame(press);
    fakeble::advanceMillis(60);
    frame({0x00, 0x00, 0x00});
    return route(fakeble::clockMs());
  }

  bleturner::Router router_;
  int media_ = -1;
};

TEST_F(RemoteBindingTest, ThreeButtonDefaultTapIsNextChapterAndHoldFrameIsPreviousChapter) {
  // The keycode bindings the page buttons were given on the old screen.
  config.nextKeyUsage = 0x02;
  config.prevKeyUsage = 0x01;
  connectThreeButton("Free3-R");

  const Routed third = tap({0x00, 0x02, 0x00});
  EXPECT_EQ(third.nextChapter, 1);
  EXPECT_EQ(third.total(), 1);

  const Routed held = tap({0x08, 0x00, 0x00});
  EXPECT_EQ(held.previousChapter, 1);
  EXPECT_EQ(held.total(), 1);

  // The page buttons keep their keycode bindings, one turn per press, no more.
  const Routed forward = tap({0x02, 0x00, 0x00});
  EXPECT_EQ(forward.next, 1);
  EXPECT_EQ(forward.total(), 1) << "the key event of the same frame turned a second page";
  const Routed back = tap({0x01, 0x00, 0x00});
  EXPECT_EQ(back.previous, 1);
  EXPECT_EQ(back.total(), 1);
}

TEST_F(RemoteBindingTest, RemoteWithoutTableKeepsTodaysPath) {
  config.nextKeyUsage = 0x02;
  config.prevKeyUsage = 0x01;
  connectThreeButton("Some Remote");
  // No table: the page buttons turn through the key path exactly as before, and
  // the third button still does nothing.
  EXPECT_EQ(tap({0x02, 0x00, 0x00}).next, 1);
  EXPECT_EQ(tap({0x01, 0x00, 0x00}).previous, 1);
  EXPECT_EQ(tap({0x00, 0x02, 0x00}).total(), 0);
  EXPECT_EQ(tap({0x08, 0x00, 0x00}).total(), 0);
}

TEST_F(RemoteBindingTest, LearningFromTheRawRingBindsEachButtonOfThisRemote) {
  connectThreeButton("Some Remote");
  // What the Bluetooth screen does: the first press edge, then its release.
  const auto learnNext = [&](bleturner::Action action, std::initializer_list<uint8_t> press, uint32_t holdMs) {
    frame(press);
    fakeble::advanceMillis(holdMs);
    frame({0x00, 0x00, 0x00});
    RawButtonEvent down, up;
    ASSERT_TRUE(fakeble::host().popRawButton(down));
    ASSERT_TRUE(fakeble::host().popRawButton(up));
    ASSERT_TRUE(down.pressed);
    ASSERT_FALSE(up.pressed);
    ASSERT_EQ(up.code(), down.code());
    KeyEvent ev;
    while (fakeble::host().popKey(ev)) {
    }
    bleturner::RemoteTable* table = bleturner::editableTable(
        config.remotes, config.remoteCount, fakeble::host().connectedAddr(), fakeble::host().connectedName());
    ASSERT_NE(table, nullptr);
    ASSERT_TRUE(bleturner::learn(*table, action, down.code(), up.atMs - down.atMs >= bleturner::kHoldMs));
  };
  learnNext(bleturner::Action::NextPage, {0x02, 0x00, 0x00}, 60);
  learnNext(bleturner::Action::PrevPage, {0x01, 0x00, 0x00}, 60);
  learnNext(bleturner::Action::NextChapter, {0x00, 0x02, 0x00}, 60);
  learnNext(bleturner::Action::PrevChapter, {0x08, 0x00, 0x00}, 60);
  ASSERT_EQ(config.remoteCount, 1);
  EXPECT_STREQ(config.remotes[0].addr, kAddr);
  EXPECT_EQ(config.remotes[0].count, 4);

  // Learned, then routed: the keycode bindings are not needed any more.
  EXPECT_EQ(tap({0x02, 0x00, 0x00}).next, 1);
  EXPECT_EQ(tap({0x01, 0x00, 0x00}).previous, 1);
  EXPECT_EQ(tap({0x00, 0x02, 0x00}).nextChapter, 1);
  EXPECT_EQ(tap({0x08, 0x00, 0x00}).previousChapter, 1);
}

TEST_F(RemoteBindingTest, TapAndHoldOnARemoteThatReportsTheRelease) {
  connectThreeButton("Some Remote");
  bleturner::RemoteTable* table = bleturner::editableTable(
      config.remotes, config.remoteCount, fakeble::host().connectedAddr(), fakeble::host().connectedName());
  ASSERT_NE(table, nullptr);
  ASSERT_TRUE(bleturner::learn(*table, bleturner::Action::NextPage, 0x030102, false));
  ASSERT_TRUE(bleturner::learn(*table, bleturner::Action::NextChapter, 0x030102, true));

  // 100 ms tap: the tap action, once, on the release.
  frame({0x00, 0x02, 0x00});
  EXPECT_EQ(route(fakeble::clockMs()).total(), 0) << "a button with a hold slot acted before it knew";
  fakeble::advanceMillis(100);
  frame({0x00, 0x00, 0x00});
  const Routed tapped = route(fakeble::clockMs());
  EXPECT_EQ(tapped.next, 1);
  EXPECT_EQ(tapped.total(), 1);

  // Held past the threshold: the hold action fires while still down, the release does nothing.
  frame({0x00, 0x02, 0x00});
  EXPECT_EQ(route(fakeble::clockMs()).total(), 0);
  fakeble::advanceMillis(800);
  const Routed held = route(fakeble::clockMs());
  EXPECT_EQ(held.nextChapter, 1);
  EXPECT_EQ(held.total(), 1);
  frame({0x00, 0x00, 0x00});
  EXPECT_EQ(route(fakeble::clockMs()).total(), 0) << "the release after a hold acted again";
}

TEST_F(RemoteBindingTest, OneRemotesTableIsNotAppliedToAnother) {
  bleturner::RemoteTable* a = bleturner::editableTable(config.remotes, config.remoteCount,
                                                         "11:22:33:44:55:66", "Remote A");
  ASSERT_NE(a, nullptr);
  ASSERT_TRUE(bleturner::learn(*a, bleturner::Action::NextChapter, 0x030102, false));
  connectThreeButton("Remote B");
  EXPECT_EQ(tap({0x00, 0x02, 0x00}).total(), 0);
  EXPECT_EQ(bleturner::tableFor(config.remotes, config.remoteCount, "11:22:33:44:55:66", "Remote A"), a);
  EXPECT_TRUE(bleturner::forgetRemote(config.remotes, config.remoteCount, "11:22:33:44:55:66"));
  EXPECT_EQ(config.remoteCount, 0);
}

TEST_F(RemoteBindingTest, ClearedTableKeepsItsAddressAndGoesBackToTheKeyPath) {
  config.nextKeyUsage = 0x02;
  connectThreeButton("Free3-R");
  bleturner::RemoteTable* table = bleturner::editableTable(
      config.remotes, config.remoteCount, fakeble::host().connectedAddr(), fakeble::host().connectedName());
  ASSERT_NE(table, nullptr);
  EXPECT_EQ(table->count, 2) << "an edit starts from the built-in default";
  bleturner::clearAction(*table, bleturner::Action::NextChapter);
  bleturner::clearAction(*table, bleturner::Action::PrevChapter);
  EXPECT_EQ(table->count, 0);
  // Cleared by hand: the default must not come back, and the key path decides.
  EXPECT_EQ(tap({0x00, 0x02, 0x00}).total(), 0);
  EXPECT_EQ(tap({0x02, 0x00, 0x00}).next, 1);
}

}  // namespace
