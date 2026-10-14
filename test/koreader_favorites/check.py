#!/usr/bin/env python3 -I
"""Do bang yeu thich 'kosync' (src/MenuFavorites.cpp) voi thu tu dong that cua man KOReader
(src/activities/settings/KOReaderSettingsActivity.cpp). Moi dong phai co dung mot khoa, nhan khoa = nhan dong."""
import re, sys
root = sys.argv[1]
fav = open(f"{root}/src/MenuFavorites.cpp", encoding="utf-8").read()
act = open(f"{root}/src/activities/settings/KOReaderSettingsActivity.cpp", encoding="utf-8").read()
table = {int(m.group(3)): (m.group(1), m.group(2))
         for m in re.finditer(r'\{"(kosync/[A-Za-z]+)",\s*StrId::(\w+),\s*"kosync",\s*0,\s*(\d+)\}', fav)}
block = re.search(r'menuNames\[[^\]]*\]\s*=\s*\{(.*?)\};', act, re.S).group(1)
rows = re.findall(r'StrId::(\w+)', block)
bad = 0
for i, label in enumerate(rows):
    key, got = table.get(i, (None, None))
    ok = got == label
    print(f"dong {i}: man hinh={label:22s} bang yeu thich={got or '(khong co khoa)':22s} khoa={key}  {'OK' if ok else 'LECH'}")
    bad += not ok
# Muc da ghim tu v1.0.60 mang khoa kosync/login va kosync/signup: hai khoa do phai con va tro dung
# dong Dang nhap, Dang ky (nhan cua khoa = nhan cua dong).
for key, label in (("kosync/login", "STR_AUTHENTICATE"), ("kosync/signup", "STR_SIGN_UP")):
    row = next((r for r, (k, _) in table.items() if k == key), None)
    ok = row is not None and rows[row] == label
    print(f"khoa cu {key}: {'OK' if ok else 'MAT hoac LECH'}")
    bad += not ok
print("DO" if bad else "XANH", bad, "dong lech")
sys.exit(1 if bad else 0)
