"""Every line the build drops from page JavaScript is a comment in code, checked by a second parser.

scripts/build_html.py drops whole lines that start with //. Its scanner is hand written, so this check asks V8
(node's built-in vm module, no npm package): the shipped script must parse, and with '#' put in front of each
dropped line it must not, because '#//' is a syntax error in code but plain text inside a template, a
backslash-continued string or a block comment. Missing node is a failure, not a skip."""
import ast
import json
import pathlib
import re
import shutil
import subprocess
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = (ROOT / 'scripts' / 'build_html.py').read_text()
TREE = ast.parse(SOURCE)
NAMES = {}
exec(''.join(ast.get_source_segment(SOURCE, node) + '\n' for node in TREE.body
             if (isinstance(node, ast.FunctionDef) and node.name == 'strip_js_comment_lines')
             or (isinstance(node, ast.Assign) and node.targets[0].id.startswith('JS_REGEX'))), NAMES)
strip = NAMES['strip_js_comment_lines']

RUNNER = r'''
const vm = require('vm');
const jobs = JSON.parse(require('fs').readFileSync(0, 'utf8'));
const parses = s => { try { new vm.Script(s); return true; } catch (e) { if (e instanceof SyntaxError) return false; throw e; } };
let bad = 0;
for (const j of jobs) {
  if (!parses(j.lines.join(''))) { console.log('BASE-FAIL', j.name); bad++; continue; }
  for (const d of j.drops) {
    const lines = j.lines.slice(); lines[d] = '#' + lines[d];
    if (parses(lines.join(''))) { console.log('NOT-A-COMMENT', j.name, d + 1, JSON.stringify(j.lines[d])); bad++; }
  }
}
process.exit(bad ? 1 : 0);
'''


def job(name, js):
    """The dropped lines of one block; the output must be the input minus whole // lines."""
    before, after = js.splitlines(keepends=True), strip(js).splitlines(keepends=True)
    drops, kept = [], 0
    for index, line in enumerate(before):
        if kept < len(after) and after[kept] == line:
            kept += 1
        else:
            assert line.lstrip(' \t').startswith('//'), (name, index + 1, line)
            drops.append(index)
    assert kept == len(after), name
    return {'name': name, 'lines': before, 'drops': drops}


def run(jobs):
    return subprocess.run(['node', '-e', RUNNER], input=json.dumps(jobs), text=True, capture_output=True)


def shipped_blocks():
    html = ROOT / 'src' / 'network' / 'html'
    for page in sorted(html.glob('*.html')):
        for m in re.finditer(r'<script\b([^>]*)>([\s\S]*?)</script>', page.read_text(), re.I):
            # vm.Script parses a classic script: modules and data blocks are not classic scripts.
            if re.search(r'type\s*=\s*["\']?(module|application/json)', m.group(1), re.I) or not m.group(2).strip():
                continue
            yield page.name, m.group(2)
    for script in sorted((html / 'js').glob('*.js')):
        if not script.name.endswith('.min.js'):
            yield script.name, script.read_text()


class StripTokens(unittest.TestCase):
    def setUp(self):
        self.assertIsNotNone(shutil.which('node'), 'node is needed for this check')

    def test_every_dropped_line_is_a_comment_in_code(self):
        jobs = [job(name, js) for name, js in shipped_blocks()]
        self.assertGreater(sum(len(j['drops']) for j in jobs), 100)
        result = run(jobs)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_the_check_sees_a_line_inside_a_template(self):
        # A scanner that took this template line for a comment would ship a broken string.
        js = 'const t = `\n// not a comment\n`;\nconst u = 1;\n'
        lines = js.splitlines(keepends=True)
        result = run([{'name': 'template', 'lines': lines, 'drops': [1]}])
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('NOT-A-COMMENT template 2', result.stdout)


if __name__ == '__main__':
    unittest.main(verbosity=2)
