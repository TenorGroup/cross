#!/usr/bin/env python3
"""Build and run the production power lifecycle contract."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


def extract_namespace(source: str, name: str) -> str:
    marker = f"namespace {name} {{"
    start = source.index(marker)
    end_marker = f"}}  // namespace {name}"
    end = source.index(end_marker, start)
    return source[start:end + len(end_marker)]


def compile_and_run(cxx: str, source: str, output: Path, name: str) -> dict:
    source_path = output / f"{name}.cpp"
    binary_path = output / name
    source_path.write_text(source)
    compiled = subprocess.run(
        [cxx, "-std=c++17", "-Wall", "-Wextra", "-Werror",
         "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
         str(source_path), "-o", str(binary_path)],
        capture_output=True, text=True)
    result = {
        "compile_returncode": compiled.returncode,
        "compile_output": compiled.stdout + compiled.stderr,
        "run_returncode": None,
        "run_output": "",
    }
    if compiled.returncode == 0:
        ran = subprocess.run([str(binary_path)], capture_output=True, text=True)
        result["run_returncode"] = ran.returncode
        result["run_output"] = ran.stdout + ran.stderr
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--source-root", type=Path)
    parser.add_argument("--cxx", default="c++")
    parser.add_argument("--expect-red", action="store_true")
    args = parser.parse_args()

    here = Path(__file__).resolve().parent
    root = (args.source_root or here.parents[1]).resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)

    paths = {
        "server_h": root / "src/network/CrossPointWebServer.h",
        "server_cpp": root / "src/network/CrossPointWebServer.cpp",
        "calibre_h": root / "src/activities/network/CalibreConnectActivity.h",
        "calibre_cpp": root / "src/activities/network/CalibreConnectActivity.cpp",
        "font_h": root / "src/activities/settings/FontDownloadActivity.h",
        "font_cpp": root / "src/activities/settings/FontDownloadActivity.cpp",
        "opds_h": root / "src/activities/browser/OpdsBookBrowserActivity.h",
        "opds_cpp": root / "src/activities/browser/OpdsBookBrowserActivity.cpp",
        "ota_h": root / "src/activities/settings/OtaUpdateActivity.h",
        "ota_cpp": root / "src/activities/settings/OtaUpdateActivity.cpp",
        "clock_h": root / "src/activities/settings/ClockSyncActivity.h",
        "clock_cpp": root / "src/activities/settings/ClockSyncActivity.cpp",
        "koreader_h": root / "src/activities/reader/KOReaderSyncActivity.h",
        "koreader_cpp": root / "src/activities/reader/KOReaderSyncActivity.cpp",
        "keyboard_h": root / "src/activities/util/KeyboardEntryActivity.h",
        "keyboard_cpp": root / "src/activities/util/KeyboardEntryActivity.cpp",
        "activity_result_h": root / "src/activities/ActivityResult.h",
    }
    sources = {name: path.read_text() for name, path in paths.items()}
    opds_search_normal_cancel_disarms_parent_idle = re.search(
        r'if \(result\.isCancelled\) \{\s*'
        r'const auto\* keyboardResult = std::get_if<KeyboardResult>\(&result\.data\);\s*'
        r'if \(keyboardResult != nullptr && keyboardResult->timedOut\) \{\s*'
        r'onGoHome\(\);\s*return;\s*\}\s*'
        r'idleTimerStarted = false;\s*state = BrowserState::BROWSING;',
        sources["opds_cpp"], re.DOTALL) is not None

    checks = [
        ("server-lifecycle", "class WebSessionLifecycle" in sources["server_h"],
         "production WebSessionLifecycle is missing"),
        ("server-api", "getLastActivityTime() const" in sources["server_h"] and
         "sessionIdleExpired(unsigned long now) const" in sources["server_h"],
         "web server idle API is missing"),
        ("request-wiring", "sessionLifecycle.noteHttpRequest" in sources["server_cpp"],
         "HTTP request lifecycle wiring is missing"),
        ("status-excluded", 'uri == "/api/status"' in sources["server_cpp"],
         "status polling is not classified separately"),
        ("ping-excluded", re.search(
            r'case WStype_PING:.*?case WStype_PONG:.*?noteWebSocketPing',
            sources["server_cpp"], re.DOTALL) is not None,
         "WebSocket ping/pong is not wired to the non-activity path"),
        ("transfer-bytes", sources["server_cpp"].count("noteTransferActivity(") >= 5,
         "one or more transfer paths do not report written bytes"),
        ("stalled-upload", "transferInProgress" not in sources["server_cpp"] and
         "transferInProgress" not in sources["server_h"],
         "an open upload can still suppress timeout forever"),
        ("http-upload-cleanup", re.search(
            r'abortHttpUploads\(\).*?upload\.file\.close\(\).*?Storage\.remove',
            sources["server_cpp"], re.DOTALL) is not None,
         "partial HTTP upload cleanup is missing"),
        ("font-upload-cleanup", re.search(
            r'abortHttpUploads\(\).*?fontUpload\.file\.close\(\).*?Storage\.remove',
            sources["server_cpp"], re.DOTALL) is not None,
         "partial font upload cleanup is missing"),
        ("stop-cleanup", re.search(
            r'void CrossPointWebServer::stop\(\).*?sessionLifecycle\.stop\(\).*?abortHttpUploads\(\)',
            sources["server_cpp"], re.DOTALL) is not None,
         "server stop does not terminate lifecycle and partial uploads"),
        ("calibre-production-order", "calibre_power::stopBeforeFinish" in sources["calibre_cpp"],
         "Calibre does not use the tested stop-before-finish path"),
        ("calibre-idle-exit", "sessionIdleExpired(millis())" in sources["calibre_cpp"],
         "Calibre does not exit an idle server session"),
        ("font-production-policy", "font_power::preventsAutoSleep" in sources["font_h"],
         "Font activity does not use the tested sleep policy"),
        ("font-terminal-timeout", "terminalStateIdleExpired(millis())" in sources["font_cpp"],
         "Font terminal screens do not release the radio on idle"),
        ("opds-idle-policy", "namespace opds_power" in sources["opds_h"] and
         "state == BrowserState::BROWSING || state == BrowserState::ERROR" in sources["opds_cpp"],
         "OPDS waiting phases do not use the production idle policy"),
        ("opds-idle-cleanup", re.search(
            r'if \(idleExitDue\(millis\(\), interaction\)\).*?onGoHome\(\)',
            sources["opds_cpp"], re.DOTALL) is not None,
         "OPDS idle exit does not route through activity cleanup"),
        ("ota-idle-policy", "namespace ota_power" in sources["ota_h"] and
         "state == WAITING_CONFIRMATION || state == FAILED || state == NO_UPDATE" in sources["ota_cpp"],
         "OTA waiting phases do not use the production idle policy"),
        ("ota-idle-cleanup", re.search(
            r'if \(idleExitDue\(millis\(\), interaction\)\).*?finish\(\)',
            sources["ota_cpp"], re.DOTALL) is not None,
         "OTA idle exit does not route through activity cleanup"),
        ("clock-idle-policy", "namespace clock_sync_power" in sources["clock_h"] and
         "state == SUCCESS || state == NO_WIFI || state == FAILED || state == TIMEZONE_FAILED" in
         sources["clock_cpp"], "Clock terminal phases do not use the production idle policy"),
        ("clock-existing-radio-cleanup", "shouldTearDownWifiOnExit = true" in sources["clock_cpp"] and
         "WiFi.disconnect(false)" in sources["clock_cpp"],
         "Clock sync does not own cleanup for an already-live radio"),
        ("koreader-idle-policy", "namespace koreader_sync_power" in sources["koreader_h"] and
         "state == SHOWING_RESULT || state == NO_REMOTE_PROGRESS || state == SYNC_FAILED" in
         sources["koreader_cpp"], "KOReader waiting phases do not use the production idle policy"),
        ("koreader-radio-gate", "runtimeStarted && wifiActivated && WiFi.getMode() != WIFI_MODE_NULL" in
         sources["koreader_cpp"], "KOReader idle deadline is not gated by a live owned radio"),
        ("keyboard-timeout-policy", "namespace keyboard_power" in sources["keyboard_h"] and
         "idleTimeoutMs = 0" in sources["keyboard_h"],
         "Keyboard opt-in timeout policy or disabled default is missing"),
        ("keyboard-timeout-result", "bool timedOut = false" in sources["activity_result_h"] and
         "KeyboardResult{\"\", true}" in sources["keyboard_cpp"],
         "Keyboard timeout does not return a distinct cancelled result"),
        ("opds-search-timeout-opt-in", re.search(
            r'KeyboardEntryActivity>\(.*?InputType::Text, true, opds_power::IDLE_TIMEOUT_MS\)',
            sources["opds_cpp"], re.DOTALL) is not None,
         "OPDS search keyboard does not opt into the five-minute timeout"),
        ("opds-search-timeout-cleanup", re.search(
            r'keyboardResult->timedOut\).*?onGoHome\(\)', sources["opds_cpp"], re.DOTALL) is not None,
         "OPDS search timeout does not route through parent cleanup"),
        ("opds-search-normal-cancel", re.search(
            r'if \(result\.isCancelled\).*?keyboardResult->timedOut.*?onGoHome\(\).*?state = BrowserState::BROWSING;.*?requestUpdate\(\)',
            sources["opds_cpp"], re.DOTALL) is not None,
         "OPDS normal keyboard cancellation does not return to the browsing list"),
    ]
    failed_checks = [{"name": name, "message": message} for name, passed, message in checks if not passed]
    check_results = [{"name": name, "passed": passed, "message": message}
                     for name, passed, message in checks]

    base_result = {
        "checks": check_results,
        "source_hashes": {str(path.relative_to(root)): hashlib.sha256(path.read_bytes()).hexdigest()
                          for path in paths.values()},
    }
    if failed_checks:
        base_result["failed_checks"] = failed_checks
        (output / "result.json").write_text(json.dumps(base_result, indent=2) + "\n")
        if args.expect_red:
            print(f"RED confirmed: {len(failed_checks)}/{len(checks)} production checks failed")
            return 0
        for failure in failed_checks:
            print(f"FAIL {failure['name']}: {failure['message']}")
        return 1
    if args.expect_red:
        print("FAIL: expected RED but all production checks passed")
        return 1

    template = (here / "PowerTimeoutPolicyTest.cpp").read_text()
    production = template.replace(
        "@SERVER_POLICY@", extract_namespace(sources["server_h"], "power_timeout")).replace(
        "@CALIBRE_POLICY@", extract_namespace(sources["calibre_h"], "calibre_power")).replace(
        "@FONT_POLICY@", extract_namespace(sources["font_h"], "font_power"))
    production = production.replace(
        "@OPDS_POLICY@", extract_namespace(sources["opds_h"], "opds_power")).replace(
        "@OTA_POLICY@", extract_namespace(sources["ota_h"], "ota_power")).replace(
        "@CLOCK_POLICY@", extract_namespace(sources["clock_h"], "clock_sync_power")).replace(
        "@KOREADER_POLICY@", extract_namespace(sources["koreader_h"], "koreader_sync_power"))
    production = production.replace(
        "@KEYBOARD_POLICY@", extract_namespace(sources["keyboard_h"], "keyboard_power"))
    production = production.replace(
        "@OPDS_SEARCH_CANCEL_DISARMS_PARENT_IDLE@",
        "true" if opds_search_normal_cancel_disarms_parent_idle else "false")

    green = compile_and_run(args.cxx, production, output, "power-timeout-lifecycle")
    if green["compile_returncode"] != 0 or green["run_returncode"] != 0:
        print(green["compile_output"] + green["run_output"], end="")
        return 1

    mutations = {
        "status-counts": (
            "running_ && authorized && !statusEndpoint",
            "running_ && authorized && (statusEndpoint || now == lastActivityTime_)"),
        "zero-byte-counts": (
            "running_ && bytes > 0",
            "running_ && (bytes > 0 || now != lastActivityTime_)"),
        "stall-never-expires": (
            "return running_ && now - lastActivityTime_ >= SESSION_IDLE_TIMEOUT_MS;",
            "return running_ && false && now - lastActivityTime_ >= SESSION_IDLE_TIMEOUT_MS;"),
        "stop-stays-running": ("void stop() { running_ = false; }", "void stop() { running_ = true; }"),
        "ping-counts": ("void noteWebSocketPing(uint32_t) {}",
                        "void noteWebSocketPing(uint32_t now) { noteMeaningfulActivity(now); }"),
        "wrap-broken": (
            "return running_ && now - lastActivityTime_ >= SESSION_IDLE_TIMEOUT_MS;",
            "return running_ && now >= lastActivityTime_ && now - lastActivityTime_ >= SESSION_IDLE_TIMEOUT_MS;"),
        "finish-before-stop": ("stop();\n  finish();", "finish();\n  stop();"),
        "font-terminal-blocks": (
            "return phase == Phase::LoadingManifest || phase == Phase::Downloading;",
            "return phase == Phase::LoadingManifest || phase == Phase::Downloading || phase == Phase::Complete ||\n"
            "         phase == Phase::Error;"),
        "activity-radio-gate": (
            "if (!waiting || !radioAlive) {",
            "if (!waiting || (radioAlive && now == idleSince)) {"),
        "activity-busy-gate": (
            "if (!waiting || !radioAlive) {",
            "if ((waiting && false) || !radioAlive) {"),
        "activity-input-reset": (
            "if (!timerStarted || interaction) {",
            "if (!timerStarted || (interaction && now == idleSince)) {"),
        "activity-wrap-broken": (
            "return now - idleSince >= IDLE_TIMEOUT_MS;",
            "return now >= idleSince && now - idleSince >= IDLE_TIMEOUT_MS;"),
        "keyboard-default-enabled": (
            "if (timeoutMs == 0) return false;",
            "if (timeoutMs == 0 && now == idleSince) return false;"),
        "keyboard-input-reset": (
            "if (interaction) {\n    idleSince = now;",
            "if (interaction && now == idleSince) {\n    idleSince = now;"),
        "keyboard-wrap-broken": (
            "return now - idleSince >= timeoutMs;",
            "return now >= idleSince && now - idleSince >= timeoutMs;"),
        "opds-search-cancel-parent-idle": (
            "constexpr bool kOpdsSearchNormalCancelDisarmsParentIdle = true;",
            "constexpr bool kOpdsSearchNormalCancelDisarmsParentIdle = false;")
    }
    mutant_results = {}
    for name, (old, new) in mutations.items():
        if old not in production:
            print(f"FAIL: mutant anchor missing: {name}")
            return 1
        mutant = production.replace(old, new, 1)
        result = compile_and_run(args.cxx, mutant, output, f"mutant-{name}")
        killed = result["compile_returncode"] == 0 and result["run_returncode"] != 0
        result["killed"] = killed
        mutant_results[name] = result
        if not killed:
            print(f"FAIL: mutant was not killed behaviorally: {name}")
            return 1

    base_result["green"] = green
    base_result["mutants"] = mutant_results
    (output / "result.json").write_text(json.dumps(base_result, indent=2) + "\n")
    (output / "result.log").write_text(green["run_output"])
    print(f"{len(checks)}/{len(checks)} production checks passed")
    print(green["run_output"], end="")
    print(f"{len(mutant_results)}/{len(mutant_results)} lifecycle mutants killed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
