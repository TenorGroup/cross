const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const zlib = require('node:zlib');
const { JSDOM, requestInterceptor, VirtualConsole } = require(process.env.JSDOM_MODULE || 'jsdom');
const repo = process.env.READER_REPO || path.resolve(__dirname, '../..');
function header(relative) {
 const text = fs.readFileSync(path.join(repo,relative),'utf8');
 const body = text.match(/constexpr char \w+\[\] PROGMEM = \{([\s\S]*?)\};/)[1];
 return zlib.gunzipSync(Buffer.from([...body.matchAll(/0x([0-9a-f]{2})/g)].map(x=>parseInt(x[1],16))));
}
const assets = {'/theme.css':'src/network/html/ThemeCss.generated.h','/js/jszip.min.js':'src/network/html/js/jszip_minJs.generated.h','/js/web-i18n.js':'src/network/html/js/WebI18nJs.generated.h'};
const loaded = [];
const resources={interceptors:[requestInterceptor(async request=>{
 const key=new URL(request.url).pathname;
 assert(assets[key],`unknown asset ${key}`);
 if(key==='/js/web-i18n.js') await new Promise(resolve=>setTimeout(resolve,10));
 loaded.push(key);
 if(key==='/js/web-i18n.js' && process.env.TENOR_TEST_DROP_HELPER==='1') return new Response('');
 return new Response(header(assets[key]),{headers:{'Content-Type':key.endsWith('.css')?'text/css':'application/javascript'}});
})]};
const pages = [['HomePage','/','/api/status'],['FilesPage','/files','/api/files'],['FontsPage','/fonts','/api/fonts'],['SettingsPage','/settings','/api/settings']];
(async()=>{
 for (const [page,route,api] of pages) for (const locale of ['vi','en-AU','zh-Hans']) {
  const errors=[],calls=[];
  loaded.length=0;
  const virtualConsole = new VirtualConsole();
  virtualConsole.on('jsdomError',e=>{ if(e.type !== 'css-parsing') errors.push(e.message); });
  virtualConsole.on('error',(...v)=>errors.push(v.map(String).join(' ')));
  const dom = new JSDOM(header(`src/network/html/${page}Html.generated.h`).toString(),{
   url:`http://reader.test${route}?lang=${locale}`,resources,runScripts:'dangerously',virtualConsole,
   beforeParse(window) {
    window.fetch=async target=>{
     assert(loaded.includes('/js/web-i18n.js'), 'bootstrap ran before its parser-blocking helper');
     const url=new URL(target,window.location.href);calls.push(url);
     const value=url.pathname==='/api/status'?{version:'fixture',serial:'host',ip:'host',freeHeap:50000}:
      url.pathname==='/api/files'?[]:url.pathname==='/api/fonts'?{families:[]}:[];
     return {ok:true,status:200,json:async()=>value};
    };
   }
  });
  await new Promise(resolve=>dom.window.addEventListener('load',resolve,{once:true}));
  await new Promise(resolve=>setTimeout(resolve,50));
  assert.deepEqual(errors,[],`${page} ${locale}`);
  assert.equal(loaded.filter(x=>x==='/js/web-i18n.js').length,1, `${page} must load the shared helper once`);
  assert.equal(dom.window.TenorI18n.locale(),locale);
  assert.equal(dom.window.document.documentElement.lang,locale);
  assert(calls.some(x=>x.pathname===api),`${page} bootstrap did not reach ${api}`);
  assert(calls.every(x=>x.searchParams.get('lang')===locale),`${page} lost request locale`);
  assert(dom.window.document.querySelector('[data-tenor-lang-selector] select'),`${page} selector missing`);
  console.log(`GREEN ${page} ${locale} ${calls.map(x=>x.pathname).join(',')}`);
  dom.window.close();
 }
})().catch(e=>{console.error(e);process.exitCode=1});
