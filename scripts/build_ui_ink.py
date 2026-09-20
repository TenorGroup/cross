#!/usr/bin/env python3
"""Offline native-weight UI ink tuning with immutable geometry and glyph guards."""
import argparse
from collections import deque
import hashlib
import json
from pathlib import Path
import re
import sys
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'scripts'))
from font_header_tools import read_header
from build_chinese_ui import SOURCE_SHA
import freetype
import numpy as np
from PIL import Image, ImageDraw

def components(mask, diagonal=False, exclude_origin=False):
    a = np.pad(mask, 1)
    seen = set()
    regions = []
    steps = [(-1, 0), (1, 0), (0, -1), (0, 1)]
    if diagonal:
        steps += [(-1, -1), (-1, 1), (1, -1), (1, 1)]
    h, w = a.shape
    for y, x in zip(*np.where(a)):
        if (y, x) in seen:
            continue
        q = deque([(y, x)])
        seen.add((y, x))
        n = 0
        contains_origin = False
        while q:
            cy, cx = q.popleft()
            n += 1
            contains_origin |= (cy == 1 and cx == 1)
            for dy, dx in steps:
                yy, xx = cy + dy, cx + dx
                if 0 <= yy < h and 0 <= xx < w and a[yy, xx] and (yy, xx) not in seen:
                    seen.add((yy, xx))
                    q.append((yy, xx))
        if not (exclude_origin and contains_origin):
            regions.append(n)
    return sorted(regions, reverse=True)

def topology(mask):
    holes = components(~np.pad(mask, 1), exclude_origin=True)
    return {'ink': int(mask.sum()), 'black_components': len(components(mask, True)),
            'holes': len(holes), 'hole_areas': holes}

def unpack(g,b):
 return np.unpackbits(np.frombuffer(b,dtype=np.uint8))[:g[0]*g[1]].reshape((g[1],g[0])).astype(bool)

def candidate(face,cp,g,threshold):
 face.load_char(cp,freetype.FT_LOAD_RENDER)
 bm=face.glyph.bitmap
 pixels=np.frombuffer(bytes(bm.buffer),dtype=np.uint8).reshape((bm.rows,abs(bm.pitch)))[:,:bm.width]>=threshold
 x=face.glyph.bitmap_left-g[3];y=g[4]-face.glyph.bitmap_top
 # Reject any ink that would fall outside the original bitmap box.
 out=np.zeros((g[1],g[0]),dtype=bool)
 for yy,xx in zip(*np.where(pixels)):
  if not(0<=y+yy<g[1] and 0<=x+xx<g[0]):return None
  out[y+yy,x+xx]=True
 return out

def safe(base,changed):
 if changed is None:return False
 b=topology(base);c=topology(changed)
 if c['holes']!=b['holes'] or c['black_components']!=b['black_components']:return False
 if not b['ink']<c['ink']<=b['ink']*1.15:return False
 if int((base & ~changed).sum())>max(1,b['ink']*.015):return False
 if any(new<max(1,old*.65) for old,new in zip(b['hole_areas'],c['hole_areas'])):return False
 return True


def tune(name, header, sources, cjk_source):
    parts=name.split('_');size=int(parts[1]);bold=parts[2]=='bold';geist=parts[0]=='geist'
    family='Geist' if geist else 'BeVietnamPro'
    filename=('Geist-Black.ttf' if bold else 'Geist-Medium.ttf') if geist else ('BeVietnamPro-ExtraBold.ttf' if bold else 'BeVietnamPro-Medium.ttf')
    path=sources/family/filename
    latin=freetype.Face(str(path));latin.set_char_size(size<<6,size<<6,150,150)
    cjk=freetype.Face(str(cjk_source));cjk.set_var_design_coords([550 if bold else 450]);cjk.set_char_size(size<<6,size<<6,150,150)
    result={};changed=[];samples={};ink_before=0;ink_after=0
    for cp,(g,b) in header['glyphs'].items():
        if not g[0]*g[1]:result[cp]=b;continue
        face=cjk if cp>=0x2e80 and cjk.get_char_index(cp) else latin
        base=unpack(g,b);chosen=base;chosen_threshold=None
        if face.get_char_index(cp):
            for threshold in (64,48,32):
                pixels=candidate(face,cp,g,threshold)
                if safe(base,pixels):chosen=pixels;chosen_threshold=threshold;break
        packed=np.packbits(chosen.reshape(-1)).tobytes()
        assert len(packed)==len(b)
        result[cp]=packed
        ink_before+=int(base.sum());ink_after+=int(chosen.sum())
        if packed!=b:
            changed.append({'codepoint':cp,'threshold':chosen_threshold,'before_sha256':hashlib.sha256(b).hexdigest(),'before_glyph_sha256':header['hashes'][str(cp)],'after_glyph_sha256':hashlib.sha256(json.dumps(g).encode()+packed).hexdigest(),'after_sha256':hashlib.sha256(packed).hexdigest()})
        if chr(cp) in 'aeo8ệậữ目器讀':samples[chr(cp)]=(base,chosen)
    stats={'glyphs':len(header['glyphs']),'changed':len(changed),'changed_glyphs':changed,'ink_gain_percent':round((ink_after/ink_before-1)*100,3),'candidate_source':path.name,'candidate_source_sha256':hashlib.sha256(path.read_bytes()).hexdigest()}
    return result,stats,samples


def write_tuned_header(source, output, glyphs):
    text=source.read_text()
    rows=re.search(r'\w+Glyphs\[.*?\]\s*=\s*\{(.*?)\n\};',text,re.S).group(1)
    records=[list(map(int,re.findall(r'-?\d+',v))) for v in re.findall(r'\{([^{}]+)\}',rows)]
    intervals=re.search(r'\w+Intervals\[.*?\]\s*=\s*\{(.*?)\n\};',text,re.S).group(1)
    intervals=[[int(x.strip(),0) for x in v.split(',')] for v in re.findall(r'\{([^{}]+)\}',intervals)]
    body=re.search(r'(\w+Bitmaps\[.*?\]\s*=\s*\{)(.*?)(\n\};)',text,re.S)
    bitmap=bytearray(int(x,16) for x in re.findall(r'0x([0-9a-fA-F]+)',body.group(2)))
    for lo,hi,first in intervals:
        for cp in range(lo,hi+1):
            record=records[first+cp-lo];length,offset=record[5:7]
            assert len(glyphs[cp])==length
            bitmap[offset:offset+length]=glyphs[cp]
    packed='\n'+'\n'.join('    '+' '.join(f'0x{x:02X},' for x in bitmap[i:i+16]) for i in range(0,len(bitmap),16))
    text=text[:body.start(2)]+packed+text[body.end(2):]
    output.write_text('// Offline UI ink tuning: scripts/build_ui_ink.py; geometry preserved.\n'+text)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--headers',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--cjk-source',type=Path,required=True)
    parser.add_argument('--sources',type=Path,default=ROOT/'lib/EpdFont/builtinFonts/source')
    args=parser.parse_args();assert args.headers.resolve()!=args.output.resolve()
    assert hashlib.sha256(args.cjk_source.read_bytes()).hexdigest()==SOURCE_SHA
    args.output.mkdir(parents=True,exist_ok=True)
    names=['bevietnampro_8_regular','bevietnampro_10_regular','bevietnampro_10_bold','geist_12_regular','geist_12_bold']
    manifest={'cjk_source_sha256':SOURCE_SHA,'cjk_weight_regular':450,'cjk_weight_bold':550,'threshold_order':[64,48,32],'fonts':{}}
    atlas=Image.new('RGB',(2200,len(names)*150),'white');draw=ImageDraw.Draw(atlas)
    for row,name in enumerate(names):
        source=args.headers/(name+'.h');header=read_header(source)
        glyphs,stats,samples=tune(name,header,args.sources,args.cjk_source)
        dest=args.output/(name+'.h');write_tuned_header(source,dest,glyphs)
        verify=read_header(dest)
        assert verify['metrics']==header['metrics'] and verify['kern']==header['kern']
        assert all(verify['glyphs'][cp][0]==original[0] for cp,original in header['glyphs'].items())
        stats['baseline_header_sha256']=hashlib.sha256(source.read_bytes()).hexdigest()
        stats['output_header_sha256']=hashlib.sha256(dest.read_bytes()).hexdigest()
        manifest['fonts'][name]=stats
        draw.text((5,row*150+5),name+' (left: baseline, right: guarded native)',fill='black')
        for column,(cp,(before,after)) in enumerate(samples.items()):
            for index,mask in enumerate((before,after)):
                image=Image.fromarray(np.where(mask,0,255).astype('uint8')).convert('RGB')
                image=image.resize((image.width*2,image.height*2),Image.Resampling.NEAREST)
                atlas.paste(image,(210+column*180+index*85,row*150+25))
        print(name,stats['changed'],stats['ink_gain_percent'],flush=True)
    (args.output/'ui-ink-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    atlas.save(args.output/'260920_tenor_cross_native-ink-atlas.png')

if __name__=='__main__':main()
