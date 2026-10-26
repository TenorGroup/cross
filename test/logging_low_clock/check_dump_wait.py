#!/usr/bin/env python3 -I
"""SCREENSHOT and LOGDUMP wait for a host that reads slowly, whatever CMD:TXTIMEOUT says.

CMD:TXTIMEOUT <ms> sets the longest one write waits for a host that stopped reading; on the X3's HWCDC port that
is divided by the 20 tries the driver makes (logSerialTimeoutFor), so the 200 ms the journey runner sends left a
dump 10 ms a try: a computer stalled for a few seconds cut a screenshot or a log dump short. The dumps raise the
timeout for their own duration (LogSerialDumpWait) and put the usual one back; log lines never wait on the port
(Logging.cpp checks the room first), so the raised wait costs them nothing.
"""
import re
import sys

root = sys.argv[1]


def read(path):
    return open(f"{root}/{path}", encoding="utf-8").read()


main, logging = read("src/main.cpp"), read("lib/Logging/Logging.cpp")
problems = []
shot = re.search(r'cmd == "SCREENSHOT"\) \{(.*?)SCREENSHOT_END', main, re.S)
if not shot or "LogSerialDumpWait" not in shot.group(1):
    problems.append("CMD:SCREENSHOT must hold a LogSerialDumpWait for the whole dump")
dump = re.search(r"void probeLogDump\(\) \{(.*?)\n\}", logging, re.S)
if not dump or "LogSerialDumpWait" not in dump.group(1):
    problems.append("probeLogDump (CMD:LOGDUMP) must hold a LogSerialDumpWait for the whole dump")
timeout = re.search(r'cmd\.startsWith\("TXTIMEOUT "\)\) \{(.*?)\n#endif', main, re.S)
if not timeout or "logSerialUsualTimeoutMs" not in timeout.group(1):
    problems.append("CMD:TXTIMEOUT must set logSerialUsualTimeoutMs, the timeout a dump puts back")
print("DO: " + "; ".join(problems) if problems else "XANH")
sys.exit(1 if problems else 0)
