#!/usr/bin/env python3 -I
"""pluginhttp::request grows its response String up to the caller's cap (32 KB for the plugin relay) with the TLS
session live, and a block from 4 KB up goes to the heap floor first (release rule 20).

On the X3 with the radio on there are about 45 KB of heap: a relay answer of 30 KB took it under the floor with no line
in the log (String::reserve only fails when the heap is gone). Each growth is asked of heapfloor::leaves before the
reserve (a realloc holds the old block and the new one), and a refusal ends the transfer as an overflow does: the
request answers -1, the relay answers 502.

The request headers are the same story from the other end: the SDK keeps a copy of every header line and builds the
request string from them (2 x the headers at its peak, one block of the whole size), and a plugin cookie of 4-8 KB
took those blocks with a plain new on a build that aborts when it fails. The app asks pluginhttp::headersFit before
the SDK is given the headers: after the handshake (openClient, the fetch setup), and up front in the relay and fetch
handlers, which answer 413 "headers too large for memory" instead of going to the network.
"""
import re
import sys

root = sys.argv[1]
text = open(f"{root}/src/util/PluginHttp.cpp", encoding="utf-8").read()
problems = []
if not re.search(r'#include\s*<HeapFloor\.h>', text):
    problems.append("PluginHttp.cpp does not include HeapFloor.h")
start = text.find("int request(freeink::SecureHttpClient* session")
reserve = text.find("out.reserve(reserved)", start)
if start < 0 or reserve < 0:
    problems.append("the response String is no longer grown by out.reserve(reserved) (this check needs updating)")
else:
    before = text[start:reserve]
    gate = re.search(r'heapfloor::leaves\(\s*[^;]*reserved[^;]*,\s*[^;]*ESP\.getFreeHeap\(\)\s*,\s*ESP\.getMaxAllocHeap\(\)\s*\)', before)
    if not gate:
        problems.append("a growth of the response String must be asked of heapfloor::leaves(held + want, want, "
                        "ESP.getFreeHeap(), ESP.getMaxAllocHeap()) before out.reserve")
    elif not re.search(r'overflow\s*=\s*true;\s*return false;', before[gate.end():]):
        problems.append("a growth the floor refuses must end the transfer (overflow = true; return false;)")

# --- the request headers, before the SDK copies them ---
if not re.search(r'bool\s+headersFit\(const Headers&[^)]*\)\s*\{[^}]*heapfloor::leaves\(', text, re.S):
    problems.append("pluginhttp::headersFit must ask heapfloor::leaves for the headers' copies before the SDK takes them")
client = text[text.find("openClient("):text.find("}  // namespace\n")]
connect = client.find("http.connect(")
guard = client.find("headersFit(headers)")
add = client.find("http.addHeader(")
if connect < 0 or add < 0 or not (connect < guard < add):
    problems.append("openClient must ask headersFit(headers) after the handshake and before addHeader")
web = open(f"{root}/src/network/CrossPointWebServer.cpp", encoding="utf-8").read()
for name, header_var in (("handleRelay", "headers"), ("handleFetch", "requestHeaders")):
    start = web.find(f"void CrossPointWebServer::{name}()")
    stop = web.find("suspendTransferServices();", start)
    body = web[start:stop]
    if start < 0 or stop < 0 or f"pluginhttp::headersFit({header_var})" not in body or "headers too large" not in body:
        problems.append(f"{name} must answer 413 'headers too large for memory' when headersFit({header_var}) says no, "
                        "before suspendTransferServices")
fetch = web[web.find("void CrossPointWebServer::handleFetch()"):]
setup = fetch.find("http.connect(feedWatchdog)")
if setup < 0 or "headersFit(requestHeaders)" not in fetch[setup:fetch.find("http.addHeader(", setup)]:
    problems.append("the fetch setup must ask headersFit(requestHeaders) after the handshake and before addHeader")
print("DO: " + "; ".join(problems) if problems else "XANH")
sys.exit(1 if problems else 0)
