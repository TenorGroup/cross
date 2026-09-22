"""Require growth headroom for the final ESP image in every application slot."""

import hashlib
from pathlib import Path
import struct


MIN_APP_HEADROOM = 128 * 1024


def check_image_size(image, partition_table):
    size = Path(image).stat().st_size
    if size == 0:
        raise ValueError("Firmware image is empty")
    data = Path(partition_table).read_bytes()
    if not data or len(data) % 32:
        raise ValueError("Partition table is empty or truncated")
    slots = []
    for start in range(0, len(data), 32):
        entry = data[start:start + 32]
        if entry == b"\xff" * 32:
            break
        if entry[:2] == b"\xeb\xeb":
            if entry[2:16] != b"\xff" * 14 or entry[16:] != hashlib.md5(data[:start]).digest():
                raise ValueError("Partition table checksum mismatch")
            break
        magic, kind, _, offset, capacity, label, _ = struct.unpack("<HBBII16sI", entry)
        if magic != 0x50AA or capacity == 0:
            raise ValueError("Invalid partition table entry")
        if kind == 0:
            name = label.split(b"\0", 1)[0].decode("ascii", errors="replace")
            slots.append((name, offset, capacity))
    if not slots:
        raise ValueError("Partition table has no application slots")
    for name, offset, capacity in slots:
        if size > capacity:
            raise ValueError(f"Firmware image {size} bytes exceeds {name} at {offset:#x} "
                             f"({capacity} bytes) by {size - capacity} bytes")
        if capacity - size < MIN_APP_HEADROOM:
            raise ValueError(f"Firmware image {size} bytes leaves {capacity - size} bytes in "
                             f"{name} at {offset:#x}; requires {MIN_APP_HEADROOM} bytes headroom")
    remaining = min(capacity for _, _, capacity in slots) - size
    print(f"Firmware image size: {size} bytes; smallest app slot headroom: {remaining} bytes "
          f"(required: {MIN_APP_HEADROOM})")


def register_check(env):
    def verify(source, target, env):
        check_image_size(str(target[0]), env.subst("$BUILD_DIR/partitions.bin"))

    # Register after the platform has created the BIN builder so the action stays
    # attached. A failure stops the build/upload, including after a factory merge.
    image = "$BUILD_DIR/${PROGNAME}.bin"
    env.Depends(image, "$BUILD_DIR/partitions.bin")
    env.Depends(image, "$PROJECT_DIR/scripts/check_firmware_size.py")
    env.AddPostAction(image, verify)


try:
    Import("env")  # noqa: F821 - injected by PlatformIO/SCons
except NameError:
    pass
else:
    register_check(env)  # noqa: F821
