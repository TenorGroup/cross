#!/usr/bin/env python3
"""Host test for the File Browser's local filename search helpers."""
import argparse
import subprocess
from pathlib import Path


def extract_function(source: str, signature: str) -> str:
    begin = source.index(signature)
    opening = source.index("{", begin)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[begin:end]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    source = args.source.read_text()
    header = args.source.with_suffix('.h').read_text()
    search_rows = next((line.strip().replace('static constexpr', 'constexpr')
                        for line in header.splitlines() if 'kSearchRows =' in line), None)
    if search_rows is None:
        raise AssertionError('File search rows need one device-specific decision')
    if "searchSourceFiles" in source or "searchSourceFiles" in header:
        raise AssertionError("search must not retain a second full name list")
    if "searchIndices" not in source or "searchIndices" not in header:
        raise AssertionError("search needs a bounded index result")
    if "buildFileSearch(screen);" not in source:
        raise AssertionError("an active no-match search must keep the Search field")
    if "requestUpdate(true)" not in source:
        raise AssertionError("the baseline callback still has an immediate render for item 3")
    for touch in (False, True):
        policy = args.output / f'search_rows_{int(touch)}.cpp'
        policy.write_text(f'namespace tenorchrome {{ constexpr bool kTouchShell = {str(touch).lower()}; }}\n'
                          + search_rows + '\nstatic_assert(kSearchRows == 0);\n')
        subprocess.run(['clang++', '-std=c++20', '-fsyntax-only', str(policy)], check=True)
    print('search rows: X3/X4=0, X4 Pro=0; separate Search field PASS')


if __name__ == "__main__":
    main()
