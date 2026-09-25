"""
PlatformIO post-build check: the firmware carries scripts/patch_sdfat.py.

The patch edits SdFat inside .pio/libdeps before compiling. A build that compiled an unpatched
copy (a libdep fetched after the pre-build script ran, a script left out of an environment)
would ship the multi-second FAT walks again without any other sign, so the ELF is checked for
the function the patch adds and the build fails without it. Also runs standalone:
    python3 scripts/check_sdfat_patch.py path/to/firmware.elf
"""

import sys

PATCH_SYMBOL = b"noteNextFree"


def check_elf(path):
    with open(path, "rb") as elf:
        if PATCH_SYMBOL not in elf.read():
            raise SystemExit(
                "ERROR: %s was built without scripts/patch_sdfat.py (no %s)" % (path, PATCH_SYMBOL.decode())
            )


def register_check(env):
    def verify(source, target, env):
        check_elf(str(target[0]))

    env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", verify)


try:
    Import("env")  # noqa: F821 - injected by PlatformIO/SCons
except NameError:
    for arg in sys.argv[1:]:
        check_elf(arg)
else:
    register_check(env)  # noqa: F821
