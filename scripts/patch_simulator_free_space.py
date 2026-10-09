"""Give the simulator card the free-space query the font pack installer asks before it unpacks.

The firmware HalStorage::freeSpace reports free bytes and the cluster size of the SD card. The
simulator card is a host folder, so the host filesystem answers. Same contract as
patch_simulator_grayscale.py: exactly one match per edit, skip an edit already applied, fail
loudly when the upstream text moved.
"""
from pathlib import Path

Import("env")
src = Path(env["PROJECT_LIBDEPS_DIR"]) / env["PIOENV"] / "simulator" / "src"

EDITS = (
    ("HalStorage.h", "  bool rmdir(const char *path);\n",
     "  bool rmdir(const char *path);\n  bool freeSpace(uint64_t &bytes, uint32_t &clusterBytes);\n"),
    ("HalStorage.cpp", None,
     "\n#include <sys/statvfs.h>\n"
     "bool HalStorage::freeSpace(uint64_t &bytes, uint32_t &clusterBytes) {\n"
     "  struct statvfs info {};\n"
     "  if (statvfs(\".\", &info) != 0 || info.f_frsize == 0) { bytes = 0; clusterBytes = 0; return false; }\n"
     "  clusterBytes = static_cast<uint32_t>(info.f_frsize);\n"
     "  bytes = static_cast<uint64_t>(info.f_bavail) * info.f_frsize;\n"
     "  return true;\n"
     "}\n"),
)

for name, old, new in EDITS:
    path = src / name
    if not path.exists():
        continue
    source = path.read_text()
    if new in source:
        continue
    if old is None:  # append: the other storage patch also anchors on the constructor line
        path.write_text(source + new)
        print(f"Added the simulator free-space query to {name}")
        continue
    if source.count(old) != 1:
        raise RuntimeError("Simulator storage source changed; review patch before building")
    path.write_text(source.replace(old, new, 1))
    print(f"Added the simulator free-space query to {name}")
