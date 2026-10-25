#!/usr/bin/env python3 -I
"""Dictionary::lookup joins the definitions of a word listed several times into one string of up to 64 KB.

Every allocation from 4 KB up goes through heapfloor (release rule 20): a lookup on the X3 with the radio on has
about 45 KB of heap, and a joined definition that is reserved without the door can take it under the floor with no
line in the log. The single-entry path already judges its buffer (readDefinition); the joined one must too, and
answer LowMemory, not abort, when the door refuses.
"""
import re
import sys

root = sys.argv[1]
text = open(f"{root}/src/util/Dictionary.cpp", encoding="utf-8").read()
reserve = text.find("definitionOut.reserve(combinedSize);")
problems = []
if reserve < 0:
    problems.append("the joined definition is no longer reserved in one piece (this check needs updating)")
else:
    after = text[reserve:reserve + 600]
    judged = re.match(
        r'definitionOut\.reserve\(combinedSize\);\s*'
        r'if \(!heapfloor::judge\(combinedSize, definitionOut\.data\(\), "[^"]+"\)\) \{'
        r'[^}]*setResult\(LookupResult::LowMemory\);[^}]*return false;',
        after,
    )
    if not judged:
        problems.append("the joined definition's buffer must be judged by heapfloor::judge right after the reserve, "
                        "answering LowMemory when it is refused")
print("DO: " + "; ".join(problems) if problems else "XANH")
sys.exit(1 if problems else 0)
