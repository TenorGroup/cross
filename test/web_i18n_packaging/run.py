#!/usr/bin/env python3
"""Run generated web assets and the production static-content response on the host."""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys


def asset(path):
    text = path.read_text()
    array = re.search(r'constexpr char \w+\[\] PROGMEM = \{(.*?)\};', text, re.S).group(1)
    data = bytes(int(n, 16) for n in re.findall(r'0x([0-9a-f]{2})', array))
    assert data[9] == 255, 'gzip OS byte must be portable: ' + str(path)
    etag = re.search(r'ETag = "\\"([0-9a-f]+)\\"";', text).group(1)
    assert etag == hashlib.sha256(data).hexdigest()[:16], path
    return data, gzip.decompress(data)


def method(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    end, depth = brace + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--repo', type=Path, default=Path(__file__).resolve().parents[2])
parser.add_argument('--output', required=True, type=Path)
parser.add_argument('--generator-python', type=Path,
                    help='also verify regeneration with the firmware build Python')
args = parser.parse_args()
repo, output = args.repo.resolve(), args.output.resolve()
output.mkdir(parents=True, exist_ok=True)
helper = repo / 'src/network/html/js/WebI18n.js'
assert helper.exists(), 'RED: shared helper asset missing; catalogue is still packaged in every page'
headers = [repo / f'src/network/html/{page}Html.generated.h'
           for page in ('HomePage', 'FilesPage', 'FontsPage', 'SettingsPage')]
helper_header = repo / 'src/network/html/js/WebI18nJs.generated.h'
compressed, decoded = asset(helper_header)
assert decoded == helper.read_bytes(), 'helper gzip must preserve every source byte'
sizes = {'WebI18nJs': len(compressed)}
for header in headers:
    compressed, decoded = asset(header)
    html = decoded.decode()
    assert 'var TenorI18n' not in html, 'RED: duplicate helper in ' + header.name
    tags = list(re.finditer(r'<script\b([^>]*)>(.*?)</script>', html, re.S))
    shared = [m for m in tags if re.search(r'src=["\']/js/web-i18n\.js["\']', m[1])]
    assert len(shared) == 1, 'each page loads one shared helper'
    assert not re.search(r'\b(async|defer)\b|type=["\']module["\']', shared[0][1]), 'helper must be parser-blocking'
    bootstrap = next(m for m in tags if 'TenorI18n.init()' in m[2])
    assert shared[0].end() <= bootstrap.start(), 'helper must load before bootstrap'
    sizes[header.stem] = len(compressed)

source = (repo / 'src/network/CrossPointWebServer.cpp').read_text()
route = re.search(r'server->on\("/js/web-i18n\.js", HTTP_GET, \[this\] \{(.*?)\n  \}\);', source, re.S)
assert route and 'auth.authorize(*server, true, requestLanguage())' in route[1] and 'handleWebI18n()' in route[1]
assert '"If-None-Match"' in source[source.index('collectedHeaders'):source.index('server->collectHeaders')]
transport = method(source, 'static void sendStaticContent(')
handler = method(source, 'void CrossPointWebServer::handleWebI18n() const')
cpp = r'''
#include <cassert>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#define PROGMEM
#include "WebI18nJs.generated.h"
struct WebServer {
  std::string incoming, body, type;
  std::map<std::string,std::string> headers;
  int status=0;
  std::string header(const char*) const { return incoming; }
  void sendHeader(const char* k,const char* v) { headers[k]=v; }
  void send(int value) { status=value; }
  void send_P(int value,const char* contentType,const char* data,size_t size) {
    status=value; type=contentType; body.assign(data,size);
  }
};
struct CrossPointWebServer {
  std::unique_ptr<WebServer> server=std::make_unique<WebServer>();
  void handleWebI18n() const;
};
''' + transport + '\n' + handler + r'''
int main() {
  for (bool matched : {false,true}) {
    CrossPointWebServer endpoint;
    auto& response=*endpoint.server;
    response.incoming=matched ? WebI18nJsETag : "stale-etag";
    endpoint.handleWebI18n();
    assert(response.status==(matched ? 304 : 200));
    assert(response.headers.at("ETag")==WebI18nJsETag);
    assert(response.headers.at("Cache-Control")=="no-cache");
    if (matched) { assert(response.body.empty()); assert(response.headers.count("Content-Encoding")==0); }
    else {
      assert(response.headers.at("Content-Encoding")=="gzip");
      assert(response.type=="application/javascript; charset=utf-8");
      assert(response.body.size()==WebI18nJsCompressedSize);
      assert(response.body==std::string(WebI18nJs,WebI18nJsCompressedSize));
      assert(static_cast<unsigned char>(response.body[0])==0x1f && static_cast<unsigned char>(response.body[1])==0x8b);
    }
  }
}
'''
(output / 'transport.cpp').write_text(cpp)
command = ['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-c++11-narrowing',
           '-fsanitize=address,undefined', '-I' + str(helper_header.parent), str(output / 'transport.cpp'),
           '-o', str(output / 'transport')]
subprocess.run(command, check=True)
subprocess.run([str(output / 'transport')], check=True)
print('GREEN production helper HTTP200/304, gzip, ETag, no-cache', flush=True)
subprocess.run(['node', str(Path(__file__).with_name('bootstrap.cjs'))],
               env={**os.environ, 'READER_REPO': str(repo)}, check=True)

before = {p: p.read_bytes() for p in (repo / 'src/network/html').rglob('*.generated.h')}
interpreters = [sys.executable, sys.executable]
if args.generator_python:
    interpreters.append(str(args.generator_python))
for interpreter in interpreters:
    result = subprocess.run([interpreter, 'scripts/build_html.py'], cwd=repo, capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    assert all(p.read_bytes() == data for p, data in before.items()), 'RED: generated gzip/ETags are not deterministic'
(output / 'sizes.json').write_text(json.dumps(sizes, indent=2) + '\n')
print('GREEN packaging: ' + json.dumps(sizes) + '; total=' + str(sum(sizes.values())) + 'B')
print('GREEN deterministic regeneration twice')
if args.generator_python:
    print('GREEN cross-runtime regeneration: ' + str(args.generator_python))
