#!/usr/bin/env python3 -I
"""pluginhttp::request grows its response String up to the caller's cap (32 KB for the plugin relay) with the TLS
session live, and a block from 4 KB up goes to the heap floor first (release rule 20).

On the X3 with the radio on there are about 45 KB of heap: a relay answer of 30 KB took it under the floor with no line
in the log (String::reserve only fails when the heap is gone). Each growth is asked of heapfloor::leaves before the
reserve (a realloc holds the old block and the new one), and a refusal ends the transfer as an overflow does: the
request answers -1, the relay answers 502.
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
print("DO: " + "; ".join(problems) if problems else "XANH")
sys.exit(1 if problems else 0)
