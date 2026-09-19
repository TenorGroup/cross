#!/usr/bin/env python3
"""Run every simulator script, including procedural jobs and unguarded unittest modules."""
import argparse
import ast
import concurrent.futures
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import runpy
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import unittest

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
DEFAULT_PROGRAM = REPO / '.pio/build/simulator_x3_uc8279/program'
# These scripts expose scenarios as PASS/FAIL lines instead of unittest methods.
PROCEDURAL = {'test_boot_home.py': 3, 'test_file_favorites.py': 15,
              'test_fixed_menu_chrome.py': 2, 'test_menu_customization.py': 16,
              'test_reader_back_recent.py': 3}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inventory():
    result = []
    for path in sorted(HERE.glob('test_*.py')):
        count = sum(isinstance(node, ast.FunctionDef) and node.name.startswith('test_')
                    for node in ast.walk(ast.parse(path.read_text())))
        result.append((path, count + PROCEDURAL.get(path.name, 0)))
    return result


def child(path, program):
    # Older tests have a fixed executable path. Keep their code unchanged while
    # all child processes use the same frozen binary, including after rebuilds.
    original = subprocess.Popen

    class FrozenPopen(original):
        def __init__(self, args, *positional, **keyword):
            if isinstance(args, (list, tuple)) and args and Path(args[0]).resolve() == DEFAULT_PROGRAM.resolve():
                args = [str(program), *args[1:]]
            super().__init__(args, *positional, **keyword)

    subprocess.Popen = FrozenPopen
    sys.path.insert(0, str(HERE))
    sys.argv = [str(path)]
    if path.name == 'test_reading_stats.py':
        # This legacy script checks existence before spawning the simulator.
        sys.argv.extend(['--program', str(program)])
    namespace = runpy.run_path(str(path), run_name='__main__')
    # unittest.main() exits itself. An unguarded module needs explicit execution.
    classes = [value for value in namespace.values()
               if isinstance(value, type) and issubclass(value, unittest.TestCase)
               and value.__module__ == '__main__']
    if classes:
        suite = unittest.TestSuite(unittest.defaultTestLoader.loadTestsFromTestCase(cls) for cls in classes)
        return int(not unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful())
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--program', type=Path, default=DEFAULT_PROGRAM)
    parser.add_argument('--output', type=Path, help='New evidence directory; must not already exist')
    parser.add_argument('--jobs', type=int, default=4)
    parser.add_argument('--timeout', type=int, default=1800, help='Seconds per test script')
    parser.add_argument('--list', action='store_true', help='List scenario counts without running')
    parser.add_argument('--child', type=Path, help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.child:
        return child(args.child, args.program)
    cases = inventory()
    if args.list:
        for path, count in cases:
            print(f'{path.name}: {count}')
        print(f'{len(cases)} scripts, {sum(count for _, count in cases)} scenarios')
        return 0
    if args.jobs < 1 or args.timeout < 1 or not cases or any(count == 0 for _, count in cases):
        parser.error('Require positive jobs/timeout and a nonempty scenario inventory for every script')
    import PIL  # Required by image comparisons.
    import freetype  # Required by real cpfont fixture conversion; skipping fails the gate.
    source = args.program.resolve(strict=True)
    out = args.output.resolve() if args.output else Path(tempfile.mkdtemp(prefix='cross-simulator-'))
    if args.output:
        out.mkdir(parents=True, exist_ok=False)
    program = out / 'program'
    shutil.copy2(source, program)
    manifest = dict(started_utc=datetime.now(timezone.utc).isoformat(), python=sys.executable,
                    source_program=str(source), program_sha256=sha(program),
                    source_head=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=REPO, text=True).strip(),
                    source_status=subprocess.check_output(['git', 'status', '--short'], cwd=REPO, text=True),
                    script_count=len(cases), expected_scenarios=sum(count for _, count in cases), results=[])

    def record():
        (out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')

    def run(case):
        path, expected = case
        started = time.monotonic()
        before = sha(path)
        fixture = out / path.stem
        fixture.mkdir()
        env = dict(os.environ, PYTHONDONTWRITEBYTECODE='1', TEST_PROGRAM=str(program),
                   STATUSBAR_PROGRAM=str(program), MENU_TEST_OUTPUT=str(fixture / 'menu'),
                   BOOT_TEST_OUTPUT=str(fixture / 'boot'), CROSSPOINT_TEST_ARTIFACTS=str(fixture / 'artifacts'))
        command = [sys.executable, str(Path(__file__).resolve()), '--child', str(path), '--program', str(program)]
        log = out / (path.stem + '.log')
        with log.open('w') as stream:
            process = subprocess.Popen(command, cwd=REPO, env=env, stdout=stream,
                                       stderr=subprocess.STDOUT, start_new_session=True)
            try:
                code = process.wait(timeout=args.timeout)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
                code = 124
                stream.write(f'\nHARNESS TIMEOUT after {args.timeout} seconds\n')
        body = log.read_text(errors='replace')
        ran = sum(int(n) for n in re.findall(r'Ran (\d+) tests?', body))
        ran += len(re.findall(r'^(?:PASS|FAIL)\b', body, re.M)) if path.name in PROCEDURAL else 0
        skipped = bool(re.search(r'skipped[= ]', body))
        passed = code == 0 and ran == expected and not skipped and before == sha(path)
        return dict(file=path.name, sha256=before, expected_scenarios=expected, actual_scenarios=ran,
                    returncode=code, passed=passed, skipped=skipped,
                    seconds=round(time.monotonic() - started, 3), log=str(log), command=command)

    print(f'Evidence: {out}', flush=True)
    record()
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for future in concurrent.futures.as_completed([pool.submit(run, case) for case in cases]):
            result = future.result()
            manifest['results'].append(result)
            record()
            print(f"{'PASS' if result['passed'] else 'FAIL'} {result['file']} "
                  f"{result['actual_scenarios']}/{result['expected_scenarios']} ({result['seconds']}s)", flush=True)
    manifest['finished_utc'] = datetime.now(timezone.utc).isoformat()
    manifest['binary_unchanged'] = sha(program) == manifest['program_sha256']
    manifest['passed_files'] = sum(result['passed'] for result in manifest['results'])
    manifest['actual_scenarios'] = sum(result['actual_scenarios'] for result in manifest['results'])
    record()
    print(f"{manifest['passed_files']}/{len(cases)} scripts passed; "
          f"{manifest['actual_scenarios']}/{manifest['expected_scenarios']} scenarios executed", flush=True)
    return int(manifest['passed_files'] != len(cases) or not manifest['binary_unchanged'])


if __name__ == '__main__':
    sys.exit(main())
