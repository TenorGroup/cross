"""Keep the simulator's absolute grayscale contract aligned with the firmware.

BitmapHelpers uses 0=black, 1=dark, 2=light, 3=white. FreeInkDisplay returns
from an absolute pass to Overlay after displayGrayBuffer or cancellation.
The upstream preview inverted the middle labels and leaked Absolute into
the next reader image. These changes affect the simulator dependency only.
"""
from pathlib import Path

Import("env")
root = Path(env["PROJECT_LIBDEPS_DIR"]) / env["PIOENV"] / "simulator" / "src" / "HalDisplay.cpp"
if root.exists():
    source = root.read_text()
    replacements = [
        ("value == 1 ? kGrayLight : value == 2 ? kGrayDark", "value == 1 ? kGrayDark : value == 2 ? kGrayLight"),
        ("  composeGrayscalePreview();\n}", "  composeGrayscalePreview();\n  grayscalePreviewState.absolute = false;\n}"),
        ("void HalDisplay::cleanupGrayscaleBuffers(const uint8_t *bwBuffer) {\n", "void HalDisplay::cleanupGrayscaleBuffers(const uint8_t *bwBuffer) {\n  grayscalePreviewState.absolute = false;\n"),
    ]
    for old, new in replacements:
        if new in source:
            continue
        if source.count(old) != 1:
            raise RuntimeError("Simulator grayscale source changed; review patch before building")
        source = source.replace(old, new, 1)
    if source != root.read_text():
        root.write_text(source)
        print("Aligned simulator absolute grayscale palette and pass completion")
