"""
PlatformIO pre-build script: fetch the FreeType source FreeTypeLite compiles against.

Why a fetch instead of vendoring: only a handful of FreeType's translation units are
built here (see lib/FreeTypeLite/src/ft_*.c), but those units `#include` most of their
sibling directory, so a useful vendored subset is ~4 MB of third-party source. The
tarball is pinned by SHA-256, so the build stays reproducible without carrying it in git.

The config headers that decide which modules exist are OURS and live in
lib/FreeTypeLite/cfg/. The upstream tree is never patched - this is the difference from
patch_wolfssl.py and patch_jpegdec.py, which do modify their libdeps.

Idempotent: a vendor tree whose .stamp matches the pinned hash is left alone.
"""

Import("env")  # noqa: F821 (SCons-injected global)
import hashlib
import os
import shutil
import sys
import tarfile
import urllib.request

VERSION = "2.13.2"
SHA256 = "1ac27e16c134a7f2ccea177faba19801131116fd682efc1f5737037c5db224b5"
URL = f"https://download.savannah.gnu.org/releases/freetype/freetype-{VERSION}.tar.gz"

# Only what the ft_*.c units pull in. Keeping this list tight is what holds the
# vendor tree near 4 MB instead of the full 7 MB of src/.
WANTED_DIRS = ("docs/FTL.TXT", "docs/GPLv2.TXT", "LICENSE.TXT", "include", "src/base", "src/sfnt", "src/truetype", "src/smooth", "src/autofit", "src/psnames")

LIB_DIR = os.path.join(env["PROJECT_DIR"], "lib", "FreeTypeLite")  # noqa: F821
VENDOR = os.path.join(LIB_DIR, "vendor")
STAMP = os.path.join(VENDOR, ".stamp")


def already_good():
    try:
        with open(STAMP) as f:
            return f.read().strip() == SHA256
    except OSError:
        return False


def fetch_freetype():
    if already_good():
        return

    print(f"FreeTypeLite: đang tải FreeType {VERSION}")
    if os.path.isdir(VENDOR):
        shutil.rmtree(VENDOR)

    try:
        with urllib.request.urlopen(URL, timeout=120) as response:
            blob = response.read()
    except Exception as exc:
        sys.stderr.write(
            f"FreeTypeLite: không tải được {URL}: {exc}\n"
            "Phần dò TTF cần mã nguồn này. Hãy tự tải tarball rồi giải nén cây\n"
            f"include/ và src/ vào {VENDOR}/ nếu máy này không có mạng.\n"
        )
        env.Exit(1)  # noqa: F821

    digest = hashlib.sha256(blob).hexdigest()
    if digest != SHA256:
        sys.stderr.write(f"FreeTypeLite: sai hash cho {URL}\n  mong đợi {SHA256}\n  nhận được {digest}\n")
        env.Exit(1)  # noqa: F821

    tmp = VENDOR + ".tmp"
    if os.path.isdir(tmp):
        shutil.rmtree(tmp)
    os.makedirs(tmp)

    scratch = os.path.join(tmp, "tarball.tar.gz")
    with open(scratch, "wb") as f:
        f.write(blob)

    prefix = f"freetype-{VERSION}/"
    keep = tuple(prefix + d for d in WANTED_DIRS)
    with tarfile.open(scratch) as tar:
        for member in tar.getmembers():
            if not member.name.startswith(keep):
                continue
            # Refuse anything that would land outside the vendor tree.
            if os.path.isabs(member.name) or ".." in member.name.split("/"):
                sys.stderr.write(f"FreeTypeLite: từ chối mục tar đáng ngờ {member.name}\n")
                env.Exit(1)  # noqa: F821
            if member.isdir() or member.isfile():
                tar.extract(member, tmp)

    os.remove(scratch)
    os.rename(os.path.join(tmp, prefix.rstrip("/")), VENDOR)
    shutil.rmtree(tmp)

    with open(STAMP, "w") as f:
        f.write(SHA256 + "\n")
    print(f"FreeTypeLite: FreeType {VERSION} đã sẵn sàng tại {VENDOR}")


fetch_freetype()
