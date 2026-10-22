#!/usr/bin/env python3
"""Check the production file-transfer entry and return contracts."""

import argparse
import hashlib
import json
import re
import subprocess
import tempfile
import unittest
from pathlib import Path


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start:index + 1]
    raise ValueError(f"unterminated function: {signature}")


class TransferEntryContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.repo = Path(__file__).resolve().parents[2]
        source_override = getattr(cls, "source_override", None)
        cls.activity_path = source_override or cls.repo / "src/activities/network/CrossPointWebServerActivity.cpp"
        cls.activity = cls.activity_path.read_text()
        cls.activity_header = (cls.repo / "src/activities/network/CrossPointWebServerActivity.h").read_text()
        cls.chooser = (cls.repo / "src/activities/network/NetworkModeSelectionActivity.cpp").read_text()
        cls.chooser_header = (cls.repo / "src/activities/network/NetworkModeSelectionActivity.h").read_text()
        cls.manager = (cls.repo / "src/activities/ActivityManager.cpp").read_text()
        cls.fonts = (cls.repo / "src/activities/settings/FontDownloadActivity.cpp").read_text()
        cls.fonts_header = (cls.repo / "src/activities/settings/FontDownloadActivity.h").read_text()
        cls.favorites = (cls.repo / "src/MenuFavorites.cpp").read_text()
        cls.main = (cls.repo / "src/main.cpp").read_text()

    def test_entry_always_opens_mode_chooser(self):
        body = function_body(self.activity, "void CrossPointWebServerActivity::onEnter()")
        self.assertRegex(body, r"\n  chooseMode\(\);\n\}$")
        self.assertIn("make_unique<NetworkModeSelectionActivity>", self.chooser_launch())
        self.assertNotIn("mappedInput.hasTouch", body)
        self.assertNotIn("startButtonOnlyFlow", body)

    def chooser_launch(self):
        return function_body(self.activity, "void CrossPointWebServerActivity::chooseMode()")

    def test_hidden_button_only_flow_is_removed(self):
        self.assertNotIn("buttonOnlyFlow", self.activity)
        self.assertNotIn("startButtonOnlyFlow", self.activity)
        self.assertNotIn("buttonOnlyFlow", self.activity_header)
        self.assertNotIn("startButtonOnlyFlow", self.activity_header)

    def test_back_from_entry_returns_home(self):
        self.assertRegex(self.chooser_launch(), r"result\.isCancelled\s*\)\s*\{\s*leave\(\)")
        # Only a session opened from a book carries a book back; Home and Settings
        # open it without one, so leave() ends on Home for them.
        entry = function_body(self.manager, "void ActivityManager::goToFileTransfer(")
        self.assertIn("isReaderActivity() ? APP_STATE.openEpubPath : std::string()", entry)
        leave = function_body(self.activity, "void CrossPointWebServerActivity::leave()")
        self.assertRegex(leave, r"^[^{]*\{\s*if \(!returnBook\.empty\(\) &&")
        self.assertTrue(leave.rstrip("} \n").endswith("onGoHome();"))
        self.assertIn("void onBackButton() override { onCancel(); }", self.chooser_header)
        cancel = function_body(self.chooser, "void NetworkModeSelectionActivity::onCancel()")
        self.assertIn("result.isCancelled = true", cancel)
        self.assertIn("finish()", cancel)

    def test_join_cancel_returns_to_same_chooser(self):
        body = function_body(self.activity, "void CrossPointWebServerActivity::onWifiSelectionComplete")
        self.assertNotIn("buttonOnlyFlow", body)
        self.assertRegex(body, r"\} else \{[^}]*chooseMode\(\);\s*\}")

    def test_calibre_return_reopens_same_chooser(self):
        body = function_body(self.activity, "void CrossPointWebServerActivity::onNetworkModeSelected")
        calibre = body.index("make_unique<CalibreConnectActivity>")
        self.assertRegex(body[calibre:], r"\{\s*state = WebServerActivityState::MODE_SELECTION;\s*chooseMode\(\);")

    # Connections (the renamed File Transfer) is the hub: "Get fonts" is its last row on every board, mapped to
    # its mode by value, since the rows a board has (USB, WebDAV) shift the indices.
    def test_font_store_row_on_every_board(self):
        enum = re.search(r"enum class NetworkMode \{(.*?)\};", self.chooser_header, re.S).group(1)
        self.assertEqual(re.findall(r"[A-Z_]+", enum)[-1], "FONT_STORE")
        rows = self.rows()
        tail = rows[rows.rindex("#endif"):]
        self.assertIn("{NetworkMode::FONT_STORE, StrId::STR_MANAGE_FONTS, StrId::STR_MANAGE_FONTS_SUB", tail)

    # A row's action value is its index (the list refuses one past its last row, which a mode number can be on a board
    # without WebDAV); the mode is read from the row table.
    def test_rows_map_to_modes_by_row(self):
        self.assertNotIn("static_cast<NetworkMode>(index)", self.chooser)
        build = function_body(self.chooser, "NetworkModeSelectionActivity::NetworkModeSelectionActivity(")
        self.assertIn("item.actionValue = static_cast<int16_t>(i)", build)
        select = function_body(self.chooser, "void NetworkModeSelectionActivity::activateIndex(")
        self.assertIn("onModeSelected(ROWS[index].mode)", select)

    def test_font_store_opens_from_hub_and_comes_back(self):
        body = function_body(self.activity, "void CrossPointWebServerActivity::onNetworkModeSelected")
        branch = body[body.index("mode == NetworkMode::FONT_STORE"):]
        branch = branch[:branch.index("return;")]
        self.assertIn("make_unique<FontDownloadActivity>(renderer, mappedInput, !returnBook.empty())", branch)
        self.assertIn("chooseMode()", branch)
        # The hub's entry already released the settings catalog and SD font caches before the store's Wi-Fi.
        enter = function_body(self.activity, "void CrossPointWebServerActivity::onEnter()")
        self.assertLess(enter.index("releaseBaseSettingsList()"), enter.index("chooseMode()"))
        self.assertLess(enter.index("releaseSdFontCaches()"), enter.index("chooseMode()"))

    # The store's Wi-Fi restarts the unit on the way out: back into the book the hub came from, else the store itself
    # (a download or an update done: the list of fonts on the card, as it is now).
    def test_font_store_restart_returns_to_store_or_book(self):
        exit_body = function_body(self.fonts, "void FontDownloadActivity::onExit()")
        self.assertIn("if (toBook_) silentRestartToReader();", exit_body)
        self.assertIn("else if (restartToStore_) silentRestartToFontStore();", exit_body)
        self.assertIn("else if (leavingStore_) silentRestartToFileTransfer();", exit_body)
        self.assertIn("else silentRestart();", exit_body)
        self.assertIn("bool toBook = false", self.fonts_header)
        self.assertRegex(self.main, r"SILENT_REBOOT_TARGET_FONTS = 5;\s*constexpr uint32_t SILENT_REBOOT_TARGET_MAX = SILENT_REBOOT_TARGET_FONTS;")
        transfer = function_body(self.main, "void silentRestartToFontStore()")
        self.assertIn("silentRestartTo(SILENT_REBOOT_TARGET_FONTS", transfer)
        self.assertRegex(self.main, r"snapshotTarget == SILENT_REBOOT_TARGET_FONTS\) \{[^}]*activityManager\.goToFileTransfer\(/\*openFontStore=\*/true\);")
        # The hub's own restart target still opens the hub.
        self.assertRegex(self.main, r"snapshotTarget == SILENT_REBOOT_TARGET_TRANSFER\) \{[^}]*activityManager\.goToFileTransfer\(\);")

    # Its old Reader row is gone; the pin "action/10" opens the store itself.
    def test_font_pin_opens_store(self):
        body = function_body(self.favorites, "std::unique_ptr<UiListActivity> open(")
        self.assertRegex(body, r'if \(key == "action/10"\) return makeUniqueNoThrow<FontDownloadActivity>\(renderer, input\);')
        settings = (self.repo / "src/activities/settings/SettingsActivity.cpp").read_text()
        self.assertNotIn("STR_MANAGE_FONTS", settings)

    def test_hotspot_branch_still_starts_access_point(self):
        body = function_body(self.activity, "void CrossPointWebServerActivity::onNetworkModeSelected")
        self.assertIn("isApMode = (mode == NetworkMode::CREATE_HOTSPOT)", body)
        self.assertIn("startAccessPoint()", body)

    def rows(self):
        rows = re.search(r"constexpr Row ROWS\[.*?= \{(.*?)\n\};", self.chooser, re.S)
        self.assertIsNotNone(rows)
        return rows.group(1)

    def test_x3_order_is_join_calibre_hotspot(self):
        ordered = self.rows()
        positions = [ordered.index(name) for name in (
            "STR_JOIN_NETWORK", "STR_CALIBRE_WIRELESS", "STR_CREATE_HOTSPOT"
        )]
        self.assertEqual(positions, sorted(positions))
        self.assertIn("props.inputMask = fui::InputTouch", self.chooser)

    def test_usb_is_fourth_and_capability_gated(self):
        self.assertIn("static constexpr int MENU_ITEM_COUNT = 6;", self.chooser_header)
        self.assertIn("#elif !FREEINK_DEVICE_X3", self.chooser_header)
        self.assertIn("static constexpr int MENU_ITEM_COUNT = 5;", self.chooser_header)
        self.assertIn("static constexpr int MENU_ITEM_COUNT = 4;", self.chooser_header)
        self.assertRegex(self.rows(), r"#if FREEINK_CAP_USB_MSC\s+\{NetworkMode::USB_DRIVE, StrId::STR_USB_DRIVE")
        branch = function_body(self.activity, "void CrossPointWebServerActivity::onNetworkModeSelected")
        self.assertRegex(branch, r"#if FREEINK_CAP_USB_MSC\s+if \(mode == NetworkMode::USB_DRIVE\)")
        self.assertIn("activityManager.goToUsbDrive()", branch)

    def test_server_back_latch_is_preserved(self):
        self.assertIn("FileTransferBackLatch backLatch", self.activity_header)
        start = function_body(self.activity, "void CrossPointWebServerActivity::startWebServer()")
        self.assertIn("backLatch.start", start)
        loop = function_body(self.activity, "void CrossPointWebServerActivity::loop()")
        self.assertIn("backLatch.consume()", loop)

    def test_idle_timeout_runs_after_handler_and_cleans_before_exit(self):
        loop = function_body(self.activity, "void CrossPointWebServerActivity::loop()")
        handled = loop.index("webServer->handleClient()")
        expired = loop.index("webServer->sessionIdleExpired(millis())")
        self.assertGreater(expired, handled)
        self.assertIn("stopServerAndLeave()", loop[expired:])
        cleanup = function_body(self.activity, "void CrossPointWebServerActivity::stopServerAndLeave()")
        order = [cleanup.index(statement) for statement in (
            "state = WebServerActivityState::SHUTTING_DOWN",
            "backLatch.stop()",
            "stopDnsServer()",
            "webServer->stop()",
            "webServer.reset()",
            "leave()",
        )]
        self.assertEqual(order, sorted(order))

    def test_web_ui_size_applier_is_injected_before_server_begin(self):
        self.assertIn('#include "UIFontTiers.h"', self.activity)
        start = function_body(self.activity, "void CrossPointWebServerActivity::startWebServer()")
        inject = start.index("webServer->setUiTextSizeApplier")
        begin = start.index("webServer->begin()")
        self.assertLess(inject, begin)
        callback = start[inject:begin]
        order = [callback.index(statement) for statement in (
            "RenderLock lock(*this)",
            "applyUiFontSize(renderer, size)",
            "SETTINGS.uiTextSize = size",
            "UITheme::getInstance().reload()",
            "requestUpdate()",
        )]
        self.assertEqual(order, sorted(order))
        apply = callback.index("applyUiFontSize(renderer, size)")
        assign = callback.index("SETTINGS.uiTextSize = size")
        self.assertIn("return false", callback[apply:assign])


def run_behavior(activity_path: Path, output: Path, cxx: str, remove_idle_guard: bool) -> bool:
    source = activity_path.read_text()
    loop = function_body(source, "void CrossPointWebServerActivity::loop()")
    if remove_idle_guard:
        pattern = re.compile(
            r"\n\s*// End an idle transfer session.*?\n\s*if \(webServer->sessionIdleExpired\(millis\(\)\)\) \{.*?\n\s*\}",
            re.S,
        )
        loop, removed = pattern.subn("", loop, count=1)
        if removed != 1:
            raise AssertionError("idle timeout guard mutant could not be created")

    output.mkdir(parents=True, exist_ok=True)
    (output / "production-loop.inc").write_text(loop)
    # Every exit, Home or back into the book it came from, runs through these two.
    (output / "production-exit.inc").write_text(
        function_body(source, "void CrossPointWebServerActivity::leave()") + "\n\n" +
        function_body(source, "void CrossPointWebServerActivity::stopServerAndLeave()") + "\n")
    binary = output / "transfer-entry-timeout"
    here = Path(__file__).resolve().parent
    compile_run = subprocess.run(
        [cxx, "-std=c++17", "-Wall", "-Wextra", "-Werror",
         "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
         "-I" + str(output), str(here / "TimeoutHarness.cpp"), "-o", str(binary)],
        capture_output=True, text=True,
    )
    if compile_run.returncode != 0:
        print(compile_run.stdout + compile_run.stderr, end="")
        return False

    cases = ("idle-timeout", "active-session", "expires-during-handler", "back-before-handler",
             "book-back-reopens-book", "book-idle-timeout-reopens-book", "book-home-gesture-goes-home",
             "book-gone-goes-home", "book-reader-alloc-fails-goes-home")
    results = []
    for case in cases:
        run = subprocess.run([str(binary), case], capture_output=True, text=True)
        results.append({"case": case, "exit": run.returncode, "log": run.stdout + run.stderr})
        print(run.stdout + run.stderr, end="")
    (output / "results.json").write_text(json.dumps({
        "mutant_remove_idle_guard": remove_idle_guard,
        "source_sha256": hashlib.sha256(activity_path.read_bytes()).hexdigest(),
        "results": results,
    }, indent=2) + "\n")
    return all(result["exit"] == 0 for result in results)


ROWS_HARNESS = r"""
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include "I18nKeys.h"
struct I18nStub {
  const char* get(StrId id) {
    static std::map<int, std::string> names;
    return names.emplace(static_cast<int>(id), std::to_string(static_cast<int>(id))).first->second.c_str();
  }
} I18N;
enum UIIcon { Wifi, Library, Hotspot, Usb, Text };
namespace freeink::ui {
struct ListItem { const char* label = nullptr; const char* subtitle = nullptr; int icon = 0; int16_t actionValue = 0; };
}
inline int listIconFor(UIIcon icon, int) { return icon; }
@@ENUM@@
struct NetworkModeSelectionActivity {
  @@COUNT@@
  freeink::ui::ListItem rowItems_[MENU_ITEM_COUNT]{};
  struct { int selected = 0; } nav;
  struct { void clearTapFlash() {} } app;
  NetworkMode chosen = NetworkMode::JOIN_NETWORK;
  NetworkModeSelectionActivity();
  void activateIndex(int index);
  void onModeSelected(NetworkMode mode) { chosen = mode; }
};
namespace fui = freeink::ui;
@@ROWS@@
@@CONSTRUCTOR@@
@@SELECT@@
int main() {
  // Each mode and the row label it must carry.
  const std::map<NetworkMode, StrId> label = {
      {NetworkMode::JOIN_NETWORK, StrId::STR_JOIN_NETWORK}, {NetworkMode::CONNECT_CALIBRE, StrId::STR_CALIBRE_WIRELESS},
      {NetworkMode::CREATE_HOTSPOT, StrId::STR_CREATE_HOTSPOT}, {NetworkMode::USB_DRIVE, StrId::STR_USB_DRIVE},
      {NetworkMode::WEB_DAV, StrId::STR_WEBDAV_PULL}, {NetworkMode::FONT_STORE, StrId::STR_MANAGE_FONTS}};
  NetworkModeSelectionActivity screen;
  bool fonts = false;
  for (int row = 0; row < NetworkModeSelectionActivity::MENU_ITEM_COUNT; ++row) {
    screen.activateIndex(row);
    if (screen.rowItems_[row].label != I18N.get(label.at(screen.chosen))) {
      std::printf("FAIL %s: row %d label %s chose mode %d\n", BOARD, row, screen.rowItems_[row].label,
                  static_cast<int>(screen.chosen));
      return 1;
    }
    fonts |= screen.chosen == NetworkMode::FONT_STORE;
  }
  if (!fonts || screen.chosen != NetworkMode::FONT_STORE) {
    std::printf("FAIL %s: Get fonts is not the last row\n", BOARD);
    return 1;
  }
  std::printf("PASS %s: %d rows, each opens the mode it names, Get fonts last\n", BOARD,
              NetworkModeSelectionActivity::MENU_ITEM_COUNT);
  return 0;
}
"""


def run_rows(repo: Path, output: Path, cxx: str) -> bool:
    """Compile the hub's row table, constructor and selection for each board's row set."""
    source = (repo / "src/activities/network/NetworkModeSelectionActivity.cpp").read_text()
    header = (repo / "src/activities/network/NetworkModeSelectionActivity.h").read_text()
    enum = re.search(r"enum class NetworkMode \{.*?\};", header, re.S).group(0)
    count = re.search(r"#if FREEINK_CAP_USB_MSC\n\s*static constexpr int MENU_ITEM_COUNT.*?#endif", header, re.S).group(0)
    rows = source[source.index("namespace {"):source.index("}  // namespace") + len("}  // namespace")]
    unit = (ROWS_HARNESS.replace("@@ENUM@@", enum).replace("@@COUNT@@", count).replace("@@ROWS@@", rows)
            .replace("@@CONSTRUCTOR@@", re.sub(r"\(GfxRenderer&[^{]*\{", "() {",  # arguments and base initializers
                                              function_body(source, "NetworkModeSelectionActivity::NetworkModeSelectionActivity(")))
            .replace("@@SELECT@@", function_body(source, "void NetworkModeSelectionActivity::activateIndex(")))
    output.mkdir(parents=True, exist_ok=True)
    (output / "rows.cpp").write_text(unit)
    ok = True
    boards = {"X3": ["-DFREEINK_DEVICE_X3=1", "-DFREEINK_CAP_USB_MSC=0"],
              "X4": ["-DFREEINK_DEVICE_X3=0", "-DFREEINK_CAP_USB_MSC=0"],
              "X4-Pro-X4C": ["-DFREEINK_DEVICE_X3=0", "-DFREEINK_CAP_USB_MSC=1"]}
    for board, flags in boards.items():
        binary = output / ("rows-" + board)
        build = subprocess.run([cxx, "-std=c++17", "-Wall", "-Werror", *flags, f'-DBOARD="{board}"',
                                "-I" + str(repo / "lib/I18n"), str(output / "rows.cpp"), "-o", str(binary)],
                               capture_output=True, text=True)
        if build.returncode != 0:
            print(build.stdout + build.stderr, end="")
            ok = False
            continue
        run = subprocess.run([str(binary)], capture_output=True, text=True)
        print(run.stdout + run.stderr, end="")
        ok &= run.returncode == 0
    return ok


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, help="Frozen CrossPointWebServerActivity.cpp for a RED run")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--cxx", default="c++")
    parser.add_argument("--mutant-remove-idle-guard", action="store_true")
    args = parser.parse_args()
    TransferEntryContract.source_override = args.source
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(TransferEntryContract)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    activity_path = args.source or Path(__file__).resolve().parents[2] / "src/activities/network/CrossPointWebServerActivity.cpp"
    if args.output:
        behavior_ok = run_behavior(activity_path, args.output.resolve(), args.cxx, args.mutant_remove_idle_guard)
    else:
        with tempfile.TemporaryDirectory(prefix="transfer-entry-") as temporary:
            behavior_ok = run_behavior(activity_path, Path(temporary), args.cxx, args.mutant_remove_idle_guard)
    with tempfile.TemporaryDirectory(prefix="transfer-rows-") as temporary:
        rows_ok = run_rows(Path(__file__).resolve().parents[2], Path(temporary), args.cxx)
    return 0 if result.wasSuccessful() and behavior_ok and rows_ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
