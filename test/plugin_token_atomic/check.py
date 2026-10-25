#!/usr/bin/env python3 -I
"""A plugin's token file is written whole to <file>.tmp and put in place by replaceFile (release rule 10).

saveTokenToFile opened the token file itself for writing: a power cut or a card fault left a cut file where the
token had been, and every later load failed until the plugin signed in again. The load side first brings back
the backup replaceFile leaves if the cut came between its two renames.
"""
import re
import sys

root = sys.argv[1]
text = open(f"{root}/src/util/PluginHttp.cpp", encoding="utf-8").read()


def body(name):
    start = text.index(name)
    depth, i = 0, text.index("{", start)
    first = i
    while True:
        depth += text[i] == "{"
        depth -= text[i] == "}"
        i += 1
        if depth == 0:
            return text[first:i]


save = body("bool saveTokenToFile(")
load = body("bool loadTokenFromFile(")
problems = []
if re.search(r'openFileForWrite\("PHTP", file\b', save):
    problems.append("saveTokenToFile opens the token file itself for writing")
if not re.search(r'openFileForWrite\("PHTP", tmp\b', save) or "freeink::replaceFile(Storage, tmp.c_str(), file.c_str())" not in save:
    problems.append("saveTokenToFile must write <file>.tmp and put it in place with freeink::replaceFile")
if "freeink::recoverFile(Storage, file.c_str())" not in load:
    problems.append("loadTokenFromFile must bring back the backup of an interrupted replace first")
print("DO: " + "; ".join(problems) if problems else "XANH")
sys.exit(1 if problems else 0)
