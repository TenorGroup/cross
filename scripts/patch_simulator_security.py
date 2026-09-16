"""Align the pinned native adapter with authenticated transfer APIs."""
from pathlib import Path
import subprocess

Import("env")
project = Path(env["PROJECT_DIR"])
root = Path(env["PROJECT_LIBDEPS_DIR"]) / env["PIOENV"] / "simulator"
patch = project / "scripts/patches/simulator-security.patch"
if root.exists():
    def check(*flags):
        return subprocess.run(["git", "apply", *flags, str(patch)], cwd=root,
                              capture_output=True, text=True)
    if check("--reverse", "--check").returncode == 0:
        print("Simulator security adapter already aligned")
    elif check("--check").returncode == 0:
        subprocess.run(["git", "apply", str(patch)], cwd=root, check=True)
        print("Aligned simulator authenticated transfer APIs")
    else:
        raise RuntimeError("Simulator patch does not match the pinned dependency; inspect upstream changes")
