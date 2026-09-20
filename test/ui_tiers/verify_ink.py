"""Verify every approved changed glyph against immutable baseline fingerprints."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from font_header_tools import read_header
import build_ui_ink as ink

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--baseline',type=Path,required=True);parser.add_argument('--headers',type=Path,required=True);parser.add_argument('--manifest',type=Path,required=True);args=parser.parse_args()
    manifest=json.loads(args.manifest.read_text())
    checked=0;changed=0
    for name,record in manifest['fonts'].items():
        before_path=args.baseline/(name+'.h');after_path=args.headers/(name+'.h')
        assert hashlib.sha256(before_path.read_bytes()).hexdigest()==record['baseline_header_sha256']
        assert hashlib.sha256(after_path.read_bytes()).hexdigest()==record['output_header_sha256']
        before=read_header(before_path);after=read_header(after_path)
        assert before['metrics']==after['metrics'] and before['kern']==after['kern']
        assert before['glyphs'].keys()==after['glyphs'].keys()
        whitelist={entry['codepoint']:entry for entry in record['changed_glyphs']}
        for cp,(metrics,pixels) in before['glyphs'].items():
            new_metrics,new_pixels=after['glyphs'][cp]
            assert metrics==new_metrics,(name,cp,'geometry')
            if pixels!=new_pixels:
                assert cp in whitelist,(name,cp,'unrecorded change')
                assert ink.safe(ink.unpack(metrics,pixels),ink.unpack(new_metrics,new_pixels)),(name,cp,'unsafe change')
                assert hashlib.sha256(pixels).hexdigest()==whitelist[cp]['before_sha256']
                assert hashlib.sha256(new_pixels).hexdigest()==whitelist[cp]['after_sha256']
                changed+=1
            else:assert cp not in whitelist
            checked+=1
    print(f'PASS: {checked} glyphs, {changed} guarded changes, unchanged metrics and kerning')
if __name__=='__main__':main()
