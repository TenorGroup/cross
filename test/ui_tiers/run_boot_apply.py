#!/usr/bin/env python3
"""Compile unchanged production apply and the current boot function body."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[2]

def function(source, marker):
    start=source.index(marker);brace=source.index('{',start);depth=1;pos=brace+1
    while depth:
        if source[pos]=='{':depth+=1
        elif source[pos]=='}':depth-=1
        pos+=1
    return source[start:pos]

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--build',type=Path,required=True)
    parser.add_argument('--mutant',choices=['omit-apply','omit-preflight','omit-cache'])
    args=parser.parse_args();args.build.mkdir(parents=True,exist_ok=True)
    main_source=(ROOT/'src/main.cpp').read_text()
    setup=function(main_source,'void setupDisplayAndFonts(')
    header=(ROOT/'lib/GfxRenderer/GfxRenderer.h').read_text()
    replace=function(header,'bool replaceBuiltinFont(')
    gfx=(ROOT/'test/ui_tiers/apply-stubs/GfxRenderer.h.in').read_text().replace('@REPLACE_BUILTIN_METHOD@',replace)
    (args.build/'GfxRenderer.h').write_text(gfx)
    apply=(ROOT/'src/UIFontTiers.cpp').read_text()
    if args.mutant=='omit-apply':setup=setup.replace('applyUiFontSize(renderer, SETTINGS.uiTextSize)','true')
    if args.mutant=='omit-preflight':
        apply=apply.replace('if (renderer.getFontMap().find(id) == renderer.getFontMap().end() || renderer.isSdCardFont(id)) return false;','')
    if args.mutant=='omit-cache':apply=apply.replace('if (auto* cache = renderer.getFontCacheManager()) cache->clearCache();','')
    # Quoted project headers resolve from src through the explicit include path.
    (args.build/'UIFontTiers.cpp').write_text(apply)
    (args.build/'boot.cpp').write_text('#include "BootApplyHarness.h"\n'+setup+'\n')
    command=['clang++','-std=c++17','-O1','-ffunction-sections','-fdata-sections','-fsanitize=address,undefined','-Wl,-dead_strip']
    for include in [args.build,ROOT/'test/ui_tiers/apply-stubs',ROOT/'test/ui_tiers',ROOT/'src',ROOT/'lib/EpdFont',ROOT/'lib/Utf8']:
        command+=['-I'+str(include)]
    command += [str(args.build/'boot.cpp'),str(args.build/'UIFontTiers.cpp'),str(ROOT/'lib/EpdFont/EpdFontFamily.cpp'),str(ROOT/'lib/EpdFont/EpdFont.cpp'),str(ROOT/'lib/Utf8/Utf8.cpp'),'-o',str(args.build/'boot-apply-test')]
    subprocess.run(command,check=True)
    cases=['apply'] if args.mutant in ('omit-preflight','omit-cache') else ['0','1','2','apply']
    results=[]
    for case in cases:
        result=subprocess.run([str(args.build/'boot-apply-test'),case],capture_output=True,text=True)
        results.append({'case':case,'returncode':result.returncode,'stdout':result.stdout,'stderr':result.stderr})
        print(case,result.returncode,result.stdout or result.stderr,flush=True)
    (args.build/'result.json').write_text(json.dumps({'main_sha256':hashlib.sha256(main_source.encode()).hexdigest(),'apply_sha256':hashlib.sha256((ROOT/'src/UIFontTiers.cpp').read_bytes()).hexdigest(),'mutant':args.mutant,'results':results},indent=2)+'\n')
    return int(any(r['returncode'] for r in results))
if __name__=='__main__':sys.exit(main())
