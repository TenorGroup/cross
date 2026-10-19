"""The fonts page names one family for a picked set of .ttf/.otf files, or reports several (runs the page's JS in node)."""
import ast
import json
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
BUILD = ROOT / 'scripts' / 'build_html.py'
tree = ast.parse(BUILD.read_text())
code = ''.join(ast.get_source_segment(BUILD.read_text(), node) + '\n' for node in tree.body
               if (isinstance(node, ast.FunctionDef) and node.name == 'strip_js_comment_lines')
               or (isinstance(node, ast.Assign) and node.targets[0].id.startswith('JS_REGEX')))
names = {}
exec(code, names)
# What ships: the page script after the build drops its comment lines.
page = (ROOT / 'src' / 'network' / 'html' / 'FontsPage.html').read_text()
script = names['strip_js_comment_lines'](max(re.findall(r'<script>(.*?)</script>', page, re.S), key=len))


def piece(name):
    m = re.search(r'\n\s*(const %s = [^\n]*;|function %s\([^)]*\) \{)' % (name, name), script)
    assert m, name
    start = m.start(1)
    if m.group(1).startswith('const'):
        return m.group(1)
    depth, i = 0, script.index('{', start)
    while True:
        depth += {'{': 1, '}': -1}.get(script[i], 0)
        i += 1
        if depth == 0:
            return script[start:i]


js = '\n'.join(piece(n) for n in ('VECTOR_RE', 'STYLE_RE', 'vectorStem', 'vectorFamilyFromFilename', 'vectorFamilyByStyle',
                                  'sanitizeFamily', 'familyFromFilename', 'familyForFile', 'familyForFiles'))
CASES = [
    (['Literata[opsz,wght].ttf', 'Literata-Italic[opsz,wght].ttf'], 'Literata'),
    (['Literata-VariableFont_opsz,wght.ttf', 'Literata-Italic-VariableFont_opsz,wght.ttf'], 'Literata'),
    (['Noto-Sans[wdth,wght].ttf', 'Noto-Sans-Italic[wdth,wght].ttf'], 'Noto-Sans'),
    (['Bookerly-Regular.ttf', 'Bookerly-Bold.ttf', 'Bookerly-Italic.ttf', 'Bookerly-BoldItalic.ttf'], 'Bookerly'),
    (['SourceSerif4-Semibold.otf', 'SourceSerif4-It.otf', 'SourceSerif4-BoldIt.otf'], 'SourceSerif4'),
    (['Roboto-regular.TTF', 'Roboto-Bold.ttf'], 'Roboto'),
    (['Inter-400.ttf', 'Inter-700.ttf'], 'Inter'),  # what the old rule already allowed stays allowed
    (['Noto_Sans-Bold.ttf'], 'Noto_Sans'),
    (['Inter.ttf'], 'Inter'),
    (['NotoSans-Regular.ttf', 'NotoSerif-Regular.ttf'], None),
    (['Roboto-Regular.ttf', 'Roboto-Slab-Regular.ttf'], None),
    (['Noto-Sans[wght].ttf', 'Noto-Serif[wght].ttf'], 'Noto'),  # the old rule's answer, kept
    (['NotoSans-Regular.ttf', 'NotoSans_Condensed-Regular.ttf'], None),
    (['Literata_18pt-Regular.ttf', 'Literata_24pt-Regular.ttf'], None),
]
runner = js + '\nconst cases = ' + json.dumps(CASES) + ''';
let bad = 0;
for (const [names, want] of cases) {
  const got = familyForFiles(names.map(name => ({ name })));
  if (got !== want) { bad++; console.log('RED', JSON.stringify(names), 'got', got, 'want', want); }
}
console.log((bad ? 'RED ' : 'GREEN ') + (cases.length - bad) + '/' + cases.length);
process.exit(bad ? 1 : 0);
'''
sys.exit(subprocess.run(['node', '-e', runner]).returncode)
