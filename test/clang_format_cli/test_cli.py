"""Exercise formatter CLI side effects inside disposable fixtures."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[2] / "bin/clang-format-fix"


class FormatterCliTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="cross-format-cli-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.bin = self.root / "bin"
        self.bin.mkdir()
        self.log = self.root / "calls.jsonl"
        self.files = ["src/one.cpp", "src/space name.cpp", "src/line\nbreak.h",
                      "lib/EpdFont/builtinFonts/generated.h", "third_party/vendor.c", "README.md"]
        for name in self.files:
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("needs_format\n")
        self.before = {name: (self.root / name).read_bytes() for name in self.files}
        # Use synthetic git output. Tests never stage or modify a real checkout.
        self.tool("git", """
args = sys.argv[1:]
names = json.loads(os.environ['FIXTURE_FILES'])
if args[0] == 'diff':
    names = ['src/space name.cpp']
elif '--modified' in args:
    names = ['src/one.cpp']
if '--' in args:
    specs = args[args.index('--') + 1:]
    if specs:
        names = [name for name in names if name in specs]
sep = '\\0' if '-z' in args else '\\n'
sys.stdout.write(sep.join(names) + (sep if names else ''))
""")
        self.formatter = """
args = sys.argv[1:]
with open(os.environ['FIXTURE_LOG'], 'a') as log:
    log.write(json.dumps(args) + '\\n')
if '--version' in args:
    print('clang-format version ' + os.environ.get('FIXTURE_VERSION', '21.0.0'))
    sys.exit(0)
names = [arg for arg in args if os.path.isfile(arg)]
if '-i' in args:
    for name in names:
        with open(name, 'w') as source:
            source.write('formatted\\n')
elif '--dry-run' in args and any(open(name).read() == 'needs_format\\n' for name in names):
    sys.exit(1)
"""
        self.tool("clang-format-21", self.formatter)
        # Keep PATH deterministic while allowing the original script's utilities.
        for name in ("bash", "cat", "grep", "head", "xargs", "mktemp", "rm", "sort"):
            target = shutil.which(name)
            if target:
                (self.bin / name).symlink_to(target)
        self.env = dict(os.environ, PATH=str(self.bin), FIXTURE_FILES=json.dumps(self.files),
                        FIXTURE_LOG=str(self.log))

    def tool(self, name, body):
        path = self.bin / name
        path.write_text(f"#!{os.sys.executable}\nimport json, os, sys\n" + body)
        path.chmod(0o755)

    def run_cli(self, *args):
        return subprocess.run(["/bin/bash", str(SCRIPT), *args], cwd=self.root,
                              env=self.env, capture_output=True, text=True)

    def calls(self):
        return [json.loads(line) for line in self.log.read_text().splitlines()] if self.log.exists() else []

    def assert_unchanged(self):
        self.assertEqual(self.before, {name: (self.root / name).read_bytes() for name in self.files})

    def test_help_exits_without_resolving_formatter_or_writing(self):
        result = self.run_cli("--help")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Usage:", result.stdout)
        self.assertEqual(self.calls(), [])
        self.assert_unchanged()

    def test_unknown_option_fails_without_writing(self):
        self.assertNotEqual(self.run_cli("--chek").returncode, 0)
        self.assertEqual(self.calls(), [])
        self.assert_unchanged()

    def test_default_checks_without_writing(self):
        self.assertEqual(self.run_cli().returncode, 1)
        self.assert_unchanged()

    def test_check_reports_dirty_format_without_writing(self):
        self.assertEqual(self.run_cli("--check").returncode, 1)
        self.assert_unchanged()

    def test_fix_requires_explicit_mode_and_preserves_path_boundaries(self):
        result = self.run_cli("--fix")
        self.assertEqual(result.returncode, 0, result.stderr)
        for name in self.files[:3]:
            self.assertEqual((self.root / name).read_text(), "formatted\n", name)
        for name in self.files[3:]:
            self.assertEqual((self.root / name).read_bytes(), self.before[name], name)
        self.assertEqual(self.run_cli("--check").returncode, 0)

    def test_explicit_path_scope(self):
        result = self.run_cli("--fix", "--", "src/space name.cpp")
        self.assertEqual(result.returncode, 0, result.stderr)
        for name in self.files:
            self.assertEqual((self.root / name).read_bytes(),
                             b"formatted\n" if name == "src/space name.cpp" else self.before[name])

    def test_changed_includes_staged_and_unstaged_files(self):
        result = self.run_cli("--fix", "-g")
        self.assertEqual(result.returncode, 0, result.stderr)
        for name in self.files:
            self.assertEqual((self.root / name).read_bytes(),
                             b"formatted\n" if name in self.files[:2] else self.before[name])

    def test_no_matching_files_succeeds_without_formatter(self):
        self.env["FIXTURE_FILES"] = json.dumps(["README.md"])
        (self.bin / "clang-format-21").unlink()
        self.assertEqual(self.run_cli("--check").returncode, 0)
        self.assert_unchanged()

    def test_xcrun_formatter_fallback(self):
        (self.bin / "clang-format-21").unlink()
        xcode = self.root / "xcode-format"
        xcode.write_text(f"#!{os.sys.executable}\nimport json, os, sys\n" + self.formatter)
        xcode.chmod(0o755)
        self.env["FIXTURE_XCODE"] = str(xcode)
        self.tool("xcrun", "print(os.environ['FIXTURE_XCODE'])\n")
        result = self.run_cli("--fix", "--", "src/one.cpp")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.root / "src/one.cpp").read_text(), "formatted\n")

    def test_old_formatter_is_rejected_before_writing(self):
        self.env["FIXTURE_VERSION"] = "20.1.0"
        self.assertNotEqual(self.run_cli("--fix").returncode, 0)
        self.assert_unchanged()

    def test_conflicting_modes_fail_before_writing(self):
        self.assertNotEqual(self.run_cli("--check", "--fix").returncode, 0)
        self.assertEqual(self.calls(), [])
        self.assert_unchanged()


if __name__ == "__main__":
    unittest.main()
