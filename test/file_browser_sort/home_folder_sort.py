"""Check both Home Folder readers pass the stored sort choice to directory reading."""
import argparse
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("--home", type=Path, required=True)
parser.add_argument("--ugly", type=Path, required=True)
args = parser.parse_args()

needle = "filebrowser::settingsSort()"
home = args.home.read_text()
ugly = args.ugly.read_text()

def function_body(text, signature):
    start = text.index(signature)
    brace = text.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]

home_body = function_body(home, "void HomeActivity::docGocTheNho()")
ugly_body = function_body(ugly, "Notebook::Rows Notebook::read(")
if needle not in home_body:
    raise SystemExit("RED: Home Folder reads the root without settingsSort")
if needle not in ugly_body:
    raise SystemExit("RED: ugly Folder reads the root without settingsSort")
if home_body.index(needle) > home_body.index("mucTheNho.insert"):
    raise SystemExit("RED: Home inserts Mo lai before sorting")
print("Folder readers use settingsSort; Mo lai remains inserted after the sorted root")
