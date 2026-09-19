"""Align the pinned native adapter with authenticated and cancellable transfer APIs."""
from pathlib import Path
import subprocess

Import("env")
project = Path(env["PROJECT_DIR"])
root = Path(env["PROJECT_LIBDEPS_DIR"]) / env["PIOENV"] / "simulator"
# The native polling adapter uses system libcurl; firmware keeps its own TLS stack.
env.AppendUnique(LIBS=["curl"])
if root.exists():
    for name in ("simulator-security.patch", "simulator-http-polling.patch"):
        patch = project / "scripts/patches" / name
        def check(*flags):
            return subprocess.run(["git", "apply", *flags, str(patch)], cwd=root,
                                  capture_output=True, text=True)
        if check("--reverse", "--check").returncode == 0:
            print(f"Simulator adapter already aligned: {name}")
        elif check("--check").returncode == 0:
            subprocess.run(["git", "apply", str(patch)], cwd=root, check=True)
            print(f"Aligned simulator adapter: {name}")
        else:
            raise RuntimeError(f"{name} does not match the pinned dependency; inspect upstream changes")
