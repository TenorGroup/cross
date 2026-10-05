"""
PlatformIO pre-build script: apply the PNGdec patches in `scripts/pngdec_patches/`.

PNGdec comes from the registry, so its libdep tree is a plain directory inside this
repository's ignored `.pio/`. `git apply` would discover the outer repository and
resolve the patch paths against it; GIT_CEILING_DIRECTORIES stops the discovery so
the patch applies relative to the PNGdec directory, as `patch -p1` would.

Idempotency follows scripts/patch_jpegdec.py:
  * `git apply --check --reverse` succeeds  -> already applied, skip
  * `git apply --check`            succeeds  -> apply
  * neither succeeds                          -> abort the build

PngToFramebufferConverter.cpp asserts the patched decoder size, so a build that
skipped this script fails to compile instead of shipping the 58 KB decoder.
"""

Import("env")  # noqa: F821 (SCons-injected global)
import os
import subprocess
import sys


PATCH_DIR = os.path.join(env["PROJECT_DIR"], "scripts", "pngdec_patches")  # noqa: F821


def patch_pngdec(env):
    libdeps_dir = os.path.join(env["PROJECT_DIR"], ".pio", "libdeps")
    if not os.path.isdir(libdeps_dir):
        return
    patches = sorted(
        os.path.join(PATCH_DIR, name) for name in os.listdir(PATCH_DIR) if name.endswith(".patch")
    )
    if not patches:
        raise RuntimeError("PNGdec patches missing -- aborting build (no .patch files in %s)" % PATCH_DIR)
    for env_dir in os.listdir(libdeps_dir):
        png_dir = os.path.join(libdeps_dir, env_dir, "PNGdec")
        if not os.path.isfile(os.path.join(png_dir, "src", "png.inl")):
            continue
        for patch in patches:
            _apply_one(png_dir, patch)


def _git_apply(png_dir, *args):
    run_env = dict(os.environ, GIT_CEILING_DIRECTORIES=os.path.dirname(png_dir))
    return subprocess.run(
        ["git", "apply", *args], cwd=png_dir, env=run_env, capture_output=True, text=True
    )


def _apply_one(png_dir, patch_path):
    name = os.path.basename(patch_path)
    if _git_apply(png_dir, "--check", "--reverse", patch_path).returncode == 0:
        return
    check = _git_apply(png_dir, "--check", patch_path)
    if check.returncode != 0:
        sys.stderr.write(
            "ERROR: PNGdec patch %s does not apply cleanly in %s:\n%s%s\n"
            % (name, png_dir, check.stdout, check.stderr)
        )
        raise SystemExit(1)
    result = _git_apply(png_dir, patch_path)
    if result.returncode != 0:
        sys.stderr.write("ERROR: PNGdec patch %s failed:\n%s\n" % (name, result.stderr))
        raise SystemExit(1)
    print("Applied PNGdec patch: %s (%s)" % (name, png_dir))


patch_pngdec(env)  # noqa: F821
