"""Let the simulator write down every card write, rename and remove.

  CROSSPOINT_SIM_SD_TRACE=1
    One line per HalStorage call that changes the card, on stderr beside the firmware log, so a
    test sees the order of card writes against the lines the firmware logs ("[SDW] remove <path>",
    "[SDW] write <path>", "[SDW] rename <old> <new>"). Unset writes nothing.

Same contract as patch_simulator_grayscale.py: exactly one match per edit, skip an edit already
applied, fail loudly when the upstream text moved.
"""
from pathlib import Path

Import("env")
path = Path(env["PROJECT_LIBDEPS_DIR"]) / env["PIOENV"] / "simulator" / "src" / "HalStorage.cpp"

HELPER = ("\nnamespace {\n"
          "void simSdTrace(const char *op, const char *a, const char *b = nullptr) {\n"
          "  static const bool on = std::getenv(\"CROSSPOINT_SIM_SD_TRACE\") != nullptr;\n"
          "  if (on) std::fprintf(stderr, \"[SDW] %s %s%s%s\\n\", op, a ? a : \"\", b ? \" \" : \"\", b ? b : \"\");\n"
          "}\n"
          "}  // namespace\n")
EDITS = (
    ("HalStorage::HalStorage() {}\n", "HalStorage::HalStorage() {}\n" + HELPER),
    ("bool HalFile::rename(const char *newPath) {\n",
     "bool HalFile::rename(const char *newPath) {\n  simSdTrace(\"rename\", impl ? impl->path.c_str() : \"\", newPath);\n"),
    ("bool HalStorage::remove(const char *path) {\n",
     "bool HalStorage::remove(const char *path) {\n  simSdTrace(\"remove\", path);\n"),
    ("bool HalStorage::rename(const char *oldPath, const char *newPath) {\n",
     "bool HalStorage::rename(const char *oldPath, const char *newPath) {\n  simSdTrace(\"rename\", oldPath, newPath);\n"),
    ("  file = open(path, O_RDWR | O_CREAT | O_TRUNC);\n",
     "  simSdTrace(\"write\", path);\n  file = open(path, O_RDWR | O_CREAT | O_TRUNC);\n"),
)

if path.exists():
    source = path.read_text()
    for old, new in EDITS:
        if new in source:
            continue
        if source.count(old) != 1:
            raise RuntimeError("Simulator storage source changed; review patch before building")
        source = source.replace(old, new, 1)
    if source != path.read_text():
        path.write_text(source)
        print("Added the simulator card-write trace")
