"""Fail ordinary builds on stale Chinese glyph coverage, without fetching fonts."""
from pathlib import Path
import sys
Import("env")
root = Path(env.subst("$PROJECT_DIR"))
sys.path.insert(0, str(root / "scripts"))
from check_chinese_ui import validate
validate(root, baseline=root / "test/chinese_ui/font-baseline.json")
