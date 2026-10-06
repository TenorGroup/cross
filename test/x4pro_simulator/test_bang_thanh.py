"""X4 Pro: every screen says what its dynamic bar holds.

Every Activity name in src/ is in one of the three bar tables of TenorMenuChrome.cpp (FULL_BAR,
BACK_ONLY_BAR, TABS_BAR) or in EXEMPT below with its reason. A new screen that forgets the bar has
no "<" on the touch shell; this test names it. Reads the source only, no build.
"""
import re
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
SRC = REPO / 'src'

EXEMPT = {
    'Boot': 'the logo at power on, no screen under it',
    'Sleep': 'the sleep screen, the device is off',
    'EpubReader': 'the reader page: the bar shows only with its menu (noteReaderFootBar)',
    'TxtReader': 'a reader page, no bar (rule 4)',
    'XtcReader': 'a reader page, no bar (rule 4)',
    'FullScreenMessage': 'the SD card error at boot, a dead end with nothing to go back to',
    'TlsAudit': 'a diagnostic build only, the blank parent of Wi-Fi',
    'UglyDiary': 'tenor/ugly root, its own nav row over the Home key',
    'UglyDesk': 'tenor/ugly root, its own nav row over the Home key',
    'UglyNotebook': 'tenor/ugly notebook, "< page" in its own nav row',
}

PATTERNS = [
    r'\b\w*(?:Activity|Screen)\(\s*"(\w+)"',                     # : Activity("Name", ...)
    r'\bname = "(\w+)"',                                         # a second name for one class
    r'make_unique<IntervalSelectionActivity>\(\s*renderer,\s*mappedInput,\s*"(\w+)"',
]


def activity_names():
    names = set()
    for path in list(SRC.rglob('*.cpp')) + list(SRC.rglob('*.h')):
        text = path.read_text(errors='ignore')
        if 'activities' not in str(path) and 'shells' not in str(path) and path.name != 'main.cpp':
            continue
        for pattern in PATTERNS:
            names.update(re.findall(pattern, text))
    return names


def table(source, name):
    body = re.search(r'constexpr const char\* ' + name + r'\[\] = \{(.*?)\};', source, re.S)
    assert body, f'{name} not found in TenorMenuChrome.cpp'
    return set(re.findall(r'"(\w+)"', body.group(1)))


def main():
    source = (SRC / 'components/TenorMenuChrome.cpp').read_text()
    tables = {n: table(source, n) for n in ('FULL_BAR', 'BACK_ONLY_BAR', 'TABS_BAR')}
    names = activity_names()
    assert len(names) > 50, f'the name scan found only {len(names)} screens'
    known = set().union(*tables.values())
    missing = sorted(names - known - set(EXEMPT))
    assert not missing, f'screens with no bar on the X4 Pro (add to a table or EXEMPT with a reason): {missing}'
    twice = sorted(n for n in known if sum(n in t for t in tables.values()) > 1)
    assert not twice, f'in two bar tables: {twice}'
    stale = sorted((known | set(EXEMPT)) - names)
    assert not stale, f'names in the tables that no screen has: {stale}'
    print(f'GREEN: {len(names)} screens, each in a bar table or exempt with a reason')


if __name__ == '__main__':
    main()
