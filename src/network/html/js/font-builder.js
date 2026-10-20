// Converts TTF/OTF files to .cpfont v4 in the browser for boards that cannot
// render vector fonts. Mirrors lib/EpdFont/scripts/fontconvert_sdcard.py:
// 150 DPI, the "builtin" intervals, 2-bit glyphs from area coverage (a // 64),
// FreeType's advances in 12.4 fixed point. No kerning or ligatures. Reader
// weights follow the store packs (scripts/build_font_pack.py): every edge moves
// by half the level's outline strength, the glyph stays centred and the layout
// stays the base file's. test/web_font_weights holds the parity check.
(function (root) {
  'use strict';
  // Even sizes like the store packs; an SD family offers the reader exactly the sizes it has.
  const SIZES = [12, 14, 16, 18, 20, 22, 24];
  // readerInk::OUTLINE_STRENGTHS (src/ReaderInkWeight.h) for the levels -3..+5, in 1/64 px.
  const STRENGTHS = [-48, -32, -18, 0, 25, 37, 63, 81, 96];
  const DEFAULT_LEVELS = [0, 1, 2, 3];
  // Glyphs are drawn 4x and averaged down: the area coverage FreeType computes, free of the browser's text
  // gamma, and a stroke a quarter pixel wide still lands on whole pixels. The averaging loop assumes 4.
  const SCALE = 4;
  const BUILTIN = [[0x0000, 0x007F], [0x0080, 0x00FF], [0x0100, 0x017F], [0x01A0, 0x01A1], [0x01AF, 0x01B0],
    [0x01C4, 0x021F], [0x0300, 0x036F], [0x0400, 0x04FF], [0x1EA0, 0x1EF9], [0x2000, 0x206F], [0x20A0, 0x20CF],
    [0x2070, 0x209F], [0x2190, 0x21FF], [0x2200, 0x22FF], [0xFB00, 0xFB06], [0xFFFD, 0xFFFD]];
  // Vietnamese letters a reader needs; a font missing any of them is reported.
  const VIET = 'ăâđêôơưàáảãạằắẳẵặầấẩẫậèéẻẽẹềếểễệìíỉĩịòóỏõọồốổỗộờớởỡợùúủũụừứửữựỳýỷỹỵ';
  const VIET_ALL = new Set([...VIET, ...VIET.toUpperCase()].map(ch => ch.codePointAt(0)));

  function parse(buf) {
    const v = new DataView(buf);
    const tag = v.getUint32(0);
    if (tag !== 0x00010000 && tag !== 0x4F54544F && tag !== 0x74727565) throw new Error('notFont');
    const tables = {};
    for (let i = 0, n = v.getUint16(4); i < n; i++) {
      const p = 12 + i * 16;
      tables[String.fromCharCode(v.getUint8(p), v.getUint8(p + 1), v.getUint8(p + 2), v.getUint8(p + 3))] = v.getUint32(p + 8);
    }
    for (const t of ['cmap', 'head', 'hhea', 'hmtx']) if (tables[t] === undefined) throw new Error('notFont');
    const head = tables.head, hhea = tables.hhea;
    const font = {
      upem: v.getUint16(head + 18),
      macStyle: v.getUint16(head + 44),
      ascender: v.getInt16(hhea + 4), descender: v.getInt16(hhea + 6), lineGap: v.getInt16(hhea + 8),
      weight: 400, italic: false, name: '', cmap: new Map(), advances: [],
    };
    const nh = v.getUint16(hhea + 34);
    for (let i = 0; i < nh; i++) font.advances.push(v.getUint16(tables.hmtx + i * 4));
    font.italic = (font.macStyle & 2) !== 0;
    if (tables['OS/2'] !== undefined) {
      font.weight = v.getUint16(tables['OS/2'] + 4);
      font.italic = font.italic || (v.getUint16(tables['OS/2'] + 62) & 1) !== 0;
    }
    readCmap(v, tables.cmap, font.cmap);
    if (tables.name !== undefined) font.name = readName(v, tables.name);
    return font;
  }

  function readCmap(v, base, out) {
    let best = -1, bestRank = 0;
    for (let i = 0, n = v.getUint16(base + 2); i < n; i++) {
      const p = base + 4 + i * 8, pid = v.getUint16(p), eid = v.getUint16(p + 2), off = base + v.getUint32(p + 4);
      const fmt = v.getUint16(off);
      const rank = fmt === 12 && (pid === 3 || pid === 0) ? 3 : fmt === 4 && (pid === 0 || (pid === 3 && eid === 1)) ? 2 : 0;
      if (rank > bestRank) { best = off; bestRank = rank; }
    }
    if (best < 0) throw new Error('notFont');
    if (v.getUint16(best) === 12) {
      for (let i = 0, n = v.getUint32(best + 12); i < n; i++) {
        const p = best + 16 + i * 12, s = v.getUint32(p), e = v.getUint32(p + 4), g = v.getUint32(p + 8);
        for (let c = s; c <= e && c <= 0xFFFF; c++) out.set(c, g + c - s);
      }
      return;
    }
    const segX2 = v.getUint16(best + 6), ends = best + 14, starts = ends + segX2 + 2, deltas = starts + segX2, ranges = deltas + segX2;
    for (let i = 0; i < segX2; i += 2) {
      const e = v.getUint16(ends + i), s = v.getUint16(starts + i), d = v.getUint16(deltas + i), ro = v.getUint16(ranges + i);
      for (let c = s; c <= e && c !== 0xFFFF; c++) {
        let g;
        if (ro === 0) g = (c + d) & 0xFFFF;
        else {
          g = v.getUint16(ranges + i + ro + (c - s) * 2);
          if (g) g = (g + d) & 0xFFFF;
        }
        if (g) out.set(c, g);
      }
    }
  }

  function readName(v, base) {
    const count = v.getUint16(base + 2), strings = base + v.getUint16(base + 4);
    let found = {};
    for (let i = 0; i < count; i++) {
      const p = base + 6 + i * 12, pid = v.getUint16(p), id = v.getUint16(p + 6), len = v.getUint16(p + 8), off = v.getUint16(p + 10);
      if (pid !== 3 || (id !== 1 && id !== 16)) continue;
      // English (0x409) first: other languages' names lose their letters to the ASCII family rule.
      const lang = v.getUint16(p + 4);
      if (found[id] && lang !== 0x409) continue;
      let s = '';
      for (let k = 0; k < len; k += 2) s += String.fromCharCode(v.getUint16(strings + off + k));
      found[id] = s;
    }
    return found[16] || found[1] || '';
  }

  // The folder a level's file lives in (readerInk::physical): weight-2..6 for +1..+5, weight-7..9 for -1..-3.
  function weightDir(level) {
    return level > 0 ? level + 1 : level < 0 ? 6 - level : 0;
  }

  // A stroke |s|/64 px wide moves every edge by |s|/128 px, as FT_Outline_EmboldenXY(s) then
  // FT_Outline_Translate(-s/2) do; erasing with it thins the glyph by as much.
  function draw(ctx, ch, x, y, strength) {
    ctx.fillText(ch, x, y);
    if (!strength) return;
    ctx.lineWidth = Math.abs(strength) * SCALE / 64;
    if (strength < 0) ctx.globalCompositeOperation = 'destination-out';
    ctx.strokeText(ch, x, y);
    ctx.globalCompositeOperation = 'source-over';
  }

  function styleOf(font) {
    return (font.weight >= 600 ? 1 : 0) + (font.italic ? 2 : 0);
  }

  // FreeType's scaled size metrics at 150 DPI: ascender rounds up, descender down, height to nearest.
  function lineMetrics(font, px) {
    const s = px / font.upem;
    return {
      advanceY: Math.round((font.ascender - font.descender + font.lineGap) * s),
      ascender: Math.ceil(font.ascender * s),
      descender: Math.floor(font.descender * s),
    };
  }

  // FreeType's linearHoriAdvance at 150 DPI in 12.4, as fontconvert_sdcard.py stores it: the 26.6 ppem is
  // rounded before the 16.16 scale is taken (FT_Request_Metrics), so px * units / upem is 1/16 off at times.
  function advance(font, pt, units) {
    const scale = Math.floor((Math.floor((pt * 64 * 150 + 36) / 72) * 65536 + (font.upem >> 1)) / font.upem);
    return Math.floor((Math.floor((units * scale + 32) / 64) + 2048) / 4096);
  }

  // One style at one size for every level in `levels` (level 0 first). Each level shares the base file's
  // intervals, advances and line metrics, maxInkTop included, so the reader lays a page out the same at
  // every weight (build_font_pack.py assert_same_layout).
  function rasterStyle(font, family, pt, ctx, levels) {
    const px = pt * 150 / 72, S = SCALE;
    ctx.font = px * S + 'px "' + family + '"';
    const intervals = [], out = levels.map(() => ({ glyphs: [], bitmapSize: 0 }));
    let maxInkTop = 0;
    // The font's own glyphs in the reader's range, plus every Vietnamese letter and U+FFFD even when the
    // font lacks them: the browser then composes the letter from the font's marks or takes a system font,
    // so no letter is dropped on the reader.
    const cps = [];
    for (const [a, b] of BUILTIN) for (let c = a; c <= b; c++) if (font.cmap.has(c) || VIET_ALL.has(c) || c === 0xFFFD) cps.push(c);
    cps.sort((x, y) => x - y);
    for (const c of cps) {
      const last = intervals[intervals.length - 1];
      if (last && last[1] === c - 1) last[1] = c; else intervals.push([c, c]);
    }
    // Each range costs the reader a few bytes of RAM per style; the fonts measured need about 75.
    if (intervals.length > 256) throw new Error('tooManyRanges');
    // Pen and crop boxes sit on whole output pixels; 3 px of margin holds the +5 stroke (0.75 px a side).
    const canvas = ctx.canvas, ox = Math.ceil(px) * S, oy = Math.ceil(px * 2) * S, cols = canvas.width / S, rows = canvas.height / S;
    for (const [a, b] of intervals) {
      for (let c = a; c <= b; c++) {
        const gid = font.cmap.get(c);
        // The soft hyphen is invisible in a browser but the reader draws it at a break.
        const ch = c === 0xAD ? '-' : String.fromCodePoint(c);
        const m = ctx.measureText(ch);
        const adv = gid === undefined ? Math.round(m.width / S * 16) : advance(font, pt, font.advances[Math.min(gid, font.advances.length - 1)]);
        const x0 = Math.max(0, Math.floor((ox - m.actualBoundingBoxLeft) / S) - 3), y0 = Math.max(0, Math.floor((oy - m.actualBoundingBoxAscent) / S) - 3);
        const x1 = Math.min(cols, Math.ceil((ox + m.actualBoundingBoxRight) / S) + 3), y1 = Math.min(rows, Math.ceil((oy + m.actualBoundingBoxDescent) / S) + 3);
        const W = x1 - x0, H = y1 - y0, WS = W * S;
        for (let k = 0; k < levels.length; k++) {
          let w = 0, h = 0, left = 0, top = 0, packed = new Uint8Array(0);
          if (W > 0 && H > 0 && /\S/.test(ch)) {
            ctx.clearRect(x0 * S, y0 * S, WS, H * S);
            draw(ctx, ch, ox, oy, STRENGTHS[levels[k] + 3]);
            // Alpha is the top byte of each little-endian RGBA word; four words make one output pixel's row.
            const d = new Uint32Array(ctx.getImageData(x0 * S, y0 * S, WS, H * S).data.buffer), sum = new Uint16Array(W * H);
            for (let y = 0, i = 0; y < H * S; y++) {
              const row = (y >> 2) * W;
              for (let x = 0; x < W; x++, i += 4) sum[row + x] += (d[i] >>> 24) + (d[i + 1] >>> 24) + (d[i + 2] >>> 24) + (d[i + 3] >>> 24);
            }
            // FreeType's 0..255 coverage over 64: the 16 samples of a pixel add up to 16 x 255.
            for (let i = 0; i < sum.length; i++) sum[i] = Math.min(3, sum[i] >> 10);
            let minX = W, minY = H, maxX = -1, maxY = -1;
            for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) if (sum[y * W + x]) {
              if (x < minX) minX = x; if (x > maxX) maxX = x; if (y < minY) minY = y; if (y > maxY) maxY = y;
            }
            if (maxX >= 0) {
              w = maxX - minX + 1; h = maxY - minY + 1;
              if (w > 255 || h > 255) throw new Error('tooLarge');
              left = x0 + minX - ox / S; top = oy / S - (y0 + minY);
              packed = new Uint8Array(Math.ceil(w * h / 4));
              for (let i = 0; i < w * h; i++) packed[i >> 2] |= sum[(minY + Math.floor(i / w)) * W + minX + i % w] << (6 - (i & 3) * 2);
              if (k === 0 && top > maxInkTop) maxInkTop = top;
            }
          }
          out[k].glyphs.push({ w, h, adv, left, top, packed, offset: out[k].bitmapSize });
          out[k].bitmapSize += packed.length;
        }
      }
    }
    const metrics = Object.assign({ intervals, maxInkTop }, lineMetrics(font, px));
    return out.map(o => Object.assign(o, metrics));
  }

  function writeCpfont(styles) {
    const ids = Object.keys(styles).map(Number).sort();
    const sizes = ids.map(id => { const s = styles[id]; return s.intervals.length * 12 + s.glyphs.length * 16 + s.bitmapSize; });
    const out = new Uint8Array(32 + ids.length * 32 + sizes.reduce((a, b) => a + b, 0));
    const v = new DataView(out.buffer);
    'CPFONT'.split('').forEach((c, i) => { out[i] = c.charCodeAt(0); });
    v.setUint16(8, 4, true); v.setUint16(10, 1, true); out[12] = ids.length;
    let data = 32 + ids.length * 32;
    ids.forEach((id, k) => {
      const s = styles[id], t = 32 + k * 32;
      if (s.advanceY > 255) throw new Error('tooLarge');
      out[t] = id; v.setUint32(t + 4, s.intervals.length, true); v.setUint32(t + 8, s.glyphs.length, true);
      out[t + 12] = s.advanceY; v.setInt16(t + 13, s.ascender, true); v.setInt16(t + 15, s.descender, true);
      v.setUint32(t + 24, data, true); v.setInt16(t + 28, s.maxInkTop, true);
      let p = data, offset = 0;
      for (const [a, b] of s.intervals) { v.setUint32(p, a, true); v.setUint32(p + 4, b, true); v.setUint32(p + 8, offset, true); p += 12; offset += b - a + 1; }
      for (const g of s.glyphs) {
        out[p] = g.w; out[p + 1] = g.h; v.setUint16(p + 2, g.adv, true); v.setInt16(p + 4, g.left, true); v.setInt16(p + 6, g.top, true);
        v.setUint16(p + 8, g.packed.length, true); v.setUint32(p + 12, g.offset, true); p += 16;
      }
      for (const g of s.glyphs) { out.set(g.packed, p); p += g.packed.length; }
      data += sizes[k];
    });
    return out;
  }

  // files: File[] (1 to 4 styles of one family); levels: reader levels -3..+5 to build, the base 0 always.
  // Returns {family, files: [{name, weight, blob}], missing}: weight is the file's weight-N folder, 0 for
  // the family folder. Base files come first, so a send cut short still leaves whole sizes.
  async function build(files, onProgress, levels) {
    levels = [0, ...new Set((levels || DEFAULT_LEVELS).filter(l => l !== 0 && STRENGTHS[l + 3] !== undefined))];
    const fonts = [];
    for (const f of files) {
      const buf = await f.arrayBuffer();
      const font = parse(buf);
      font.style = styleOf(font);
      if (fonts.some(o => o.style === font.style)) throw new Error('duplicateStyle');
      font.face = 'tfb' + Date.now() + fonts.length;
      const face = new FontFace(font.face, buf);
      await face.load();
      document.fonts.add(face);
      fonts.push(font);
    }
    if (!fonts.length) throw new Error('notFont');
    // The reader draws a family from its regular style: a lone bold or italic file becomes the regular one.
    if (!fonts.some(f => f.style === 0)) fonts.sort((a, b) => a.style - b.style)[0].style = 0;
    const base = fonts.find(f => f.style === 0);
    // The font's own name: a store download never replaces a family no pack made (FontPackInstaller::isPackFamily).
    const family = (base.name || files[0].name.replace(/\.[^.]+$/, '')).replace(/[^A-Za-z0-9_-]/g, '').slice(0, 31) || 'MyFont';
    const missing = Math.max(...fonts.map(f => [...VIET_ALL].filter(c => !f.cmap.has(c)).length));
    const canvas = document.createElement('canvas');
    canvas.width = canvas.height = Math.ceil(SIZES[SIZES.length - 1] * 150 / 72 * 4) * SCALE;
    const ctx = canvas.getContext('2d', { willReadFrequently: true });
    ctx.textBaseline = 'alphabetic';
    ctx.fillStyle = '#000';
    // Private browsing in some browsers adds noise to canvas reads, which would speckle every glyph.
    ctx.fillRect(0, 0, 8, 8);
    const probe = ctx.getImageData(0, 0, 16, 16).data;
    for (let i = 0; i < 256; i++) if (probe[i * 4 + 3] !== ((i & 15) < 8 && i < 128 ? 255 : 0)) throw new Error('noisyCanvas');
    ctx.clearRect(0, 0, 16, 16);
    const out = levels.map(() => []);
    for (let i = 0; i < SIZES.length; i++) {
      const styles = levels.map(() => ({}));
      for (const font of fonts) rasterStyle(font, font.face, SIZES[i], ctx, levels).forEach((s, k) => { styles[k][font.style] = s; });
      levels.forEach((level, k) => out[k].push({ name: family + '_' + SIZES[i] + '.cpfont', weight: weightDir(level), blob: new Blob([writeCpfont(styles[k])]) }));
      if (onProgress) onProgress((i + 1) / SIZES.length);
      await new Promise(r => setTimeout(r, 0));
    }
    fonts.forEach(f => document.fonts.forEach(face => { if (face.family === f.face) document.fonts.delete(face); }));
    return { family, files: out.flat(), missing };
  }

  root.TenorFontBuilder = { build, parse, writeCpfont, rasterStyle, weightDir, SIZES, STRENGTHS, DEFAULT_LEVELS };
})(typeof window !== 'undefined' ? window : globalThis);
