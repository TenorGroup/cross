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

    def test_entry_always_opens_mode_chooser(self):
        body = function_body(self.activity, "void CrossPointWebServerActivity::onEnter()")
        self.assertIn("make_unique<NetworkModeSelectionActivity>", body)
        self.assertNotIn("mappedInput.hasTouch", body)
        self.assertNotIn("startButtonOnlyFlow", body)

    def test_hidden_button_only_flow_is_removed(self):
        self.assertNotIn("buttonOnlyFlow", self.activity)
        self.assertNotIn("startButtonOnlyFlow", self.activity)
        self.assertNotIn("buttonOnlyFlow", self.activity_header)
        self.assertNotIn("startButtonOnlyFlow", self.activity_header)

    def test_back_from_entry_returns_home(self):
        enter = function_body(self.activity, "void CrossPointWebServerActivity::onEnter()")
        self.assertRegex(enter, r"result\.isCancelled\s*\)\s*\{\s*onGoHome\(\)")
        self.assertIn("void onBackButton() override { onCancel(); }", self.chooser_header)
        cancel = function_body(self.chooser, "void NetworkModeSelectionActivity::onCancel()")
        self.assertIn("result.isCancelled = true", cancel)
        self.assertIn("finish()", cancel)

    def test_join_cancel_returns_to_same_chooser(self):
        body = function_body(self.activity, "void CrossPointWebServerActivity::onWifiSelectionComplete")
        self.assertNotIn("buttonOnlyFlow", body)
        self.assertIn("make_unique<NetworkModeSelectionActivity>", body)
        self.assertRegex(body, r"result\.isCancelled\s*\)\s*\{\s*onGoHome\(\)")

    def test_calibre_return_reopens_same_chooser(self):
        body = function_body(self.activity, "void CrossPointWebServerActivity::onNetworkModeSelected")
        calibre = body.index("mode == NetworkMode::CONNECT_CALIBRE")
        reopen = body.index("make_unique<NetworkModeSelectionActivity>", calibre)
        self.assertGreater(reopen, calibre)
        self.assertIn("onGoHome()", body[reopen:])

    def test_hotspot_branch_still_starts_access_point(self):
        body = function_body(self.activity, "void CrossPointWebServerActivity::onNetworkModeSelected")
        self.assertIn("isApMode = (mode == NetworkMode::CREATE_HOTSPOT)", body)
        self.assertIn("startAccessPoint()", body)

    def test_x3_order_is_join_calibre_hotspot(self):
        items = re.search(r"constexpr StrId menuItems.*?= \{(.*?)\n\};", self.chooser, re.S)
        self.assertIsNotNone(items)
        ordered = items.group(1)
        positions = [ordered.index(name) for name in (
            "STR_JOIN_NETWORK", "STR_CALIBRE_WIRELESS", "STR_CREATE_HOTSPOT"
        )]
        self.assertEqual(positions, sorted(positions))
        self.assertIn("props.inputMask = fui::InputTouch", self.chooser)

    def test_usb_is_fourth_and_capability_gated(self):
        self.assertRegex(
            self.chooser_header,
            r"#if FREEINK_CAP_USB_MSC\s+static constexpr int MENU_ITEM_COUNT = 4;\s+#else\s+"
            r"static constexpr int MENU_ITEM_COUNT = 3;",
        )
        items = re.search(r"constexpr StrId menuItems.*?= \{(.*?)\n\};", self.chooser, re.S).group(1)
        self.assertRegex(items, r"#if FREEINK_CAP_USB_MSC\s+StrId::STR_USB_DRIVE")
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
        self.assertIn("stopServerAndGoHome()", loop[expired:])
        cleanup = function_body(self.activity, "void CrossPointWebServerActivity::stopServerAndGoHome()")
        order = [cleanup.index(statement) for statement in (
            "state = WebServerActivityState::SHUTTING_DOWN",
            "backLatch.stop()",
            "stopDnsServer()",
            "webServer->stop()",
            "webServer.reset()",
            "onGoHome()",
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

    cases = ("idle-timeout", "active-session", "expires-during-handler", "back-before-handler")
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
    return 0 if result.wasSuccessful() and behavior_ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
