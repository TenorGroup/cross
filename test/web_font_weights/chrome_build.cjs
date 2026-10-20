// Runs the fonts page's TTF converter (src/network/html/js/font-builder.js) in headless Chrome and writes
// what it would upload: <out>/<file> and <out>/weight-N/<file>. Prints {ms, bytes, files} as JSON.
// node chrome_build.cjs <font-builder.js> <out dir> <levels JSON> <font.ttf>...
// Needs playwright-core on NODE_PATH and Chrome (CHROME, default the macOS app path).
const fs = require('fs');
const path = require('path');
const { chromium } = require('playwright-core');

(async () => {
  const [builder, outDir, levels, ...fonts] = process.argv.slice(2);
  const browser = await chromium.launch({
    executablePath: process.env.CHROME || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
  });
  try {
    const page = await browser.newPage();
    await page.setContent('<!doctype html><meta charset="utf-8">');
    await page.addScriptTag({ content: fs.readFileSync(builder, 'utf8') });
    const inputs = fonts.map(f => ({ name: path.basename(f), b64: fs.readFileSync(f).toString('base64') }));
    const result = await page.evaluate(async ([inputs, levels]) => {
      const files = inputs.map(({ name, b64 }) => new File([Uint8Array.from(atob(b64), c => c.charCodeAt(0))], name));
      const t = performance.now();
      const built = await TenorFontBuilder.build(files, null, levels);
      const ms = performance.now() - t;
      const out = [];
      for (const f of built.files) {
        const bytes = new Uint8Array(await f.blob.arrayBuffer());
        let s = '';
        for (let i = 0; i < bytes.length; i += 8192) s += String.fromCharCode.apply(null, bytes.subarray(i, i + 8192));
        out.push({ name: f.name, weight: f.weight, b64: btoa(s) });
      }
      return { ms, family: built.family, files: out };
    }, [inputs, JSON.parse(levels)]);
    let bytes = 0;
    for (const f of result.files) {
      const dir = f.weight ? path.join(outDir, 'weight-' + f.weight) : outDir;
      fs.mkdirSync(dir, { recursive: true });
      const data = Buffer.from(f.b64, 'base64');
      fs.writeFileSync(path.join(dir, f.name), data);
      bytes += data.length;
    }
    console.log(JSON.stringify({ ms: Math.round(result.ms), bytes, files: result.files.length, family: result.family }));
  } finally {
    await browser.close();
  }
})().catch(e => { console.error(e); process.exit(1); });
