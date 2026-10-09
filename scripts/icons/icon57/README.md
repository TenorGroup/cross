# 40 px icon alignment

`png/` contains the 42 source images used by `scripts/icons/sinh.py`. Each image is 1 bit and 40x40 px.

The 21 designs were approved on 2026-10-09. `-xam` contains compensated grey pixels; `-chon` contains the selected stroke-3 artwork. Grey straight bands span 4 px. The selected central stems of `doc` and `thong_tin` remain 4 px wide for symmetry.

Centre the ink bounding box. Opposite white margins must differ by at most 1 px. An odd ink extent leaves the extra white pixel at the right or bottom. The ink centre is therefore 19.5 px for an even extent and 19 px for an odd extent. Selected and grey variants follow this same convention; different extent parity permits a 0.5 px centre difference.

Translate whole stroke masks. Preserve mirror symmetry. Grey pixels use the original `ve.py` checker rule: `(x + y) % 2 == 0`, with `x = min(x, 39 - x)` for symmetric icons. Regenerate the checker on translated original masks when an odd translation would change its phase. Font moves up 3 px in both variants; its grey variant uses the regenerated checker. Keyboard grey moves up 1 px and uses the regenerated checker. The remaining changes are whole-bitmap translations.

`tenorchrome::drawBarTab` measures the bitmap's ink bounding box and centres it inside the selected ring or the same cell rectangle. It leaves a spare pixel at right/bottom. It does not use `freeink::Icon::opticalCenterY`, the rounded mean ink row.

The `Icon57SourceAlignment`, `Icon57X3Alignment` and `Icon57TouchAlignment` host tests enforce these rules. The generator also rejects source margins outside the convention.

Run `python3 scripts/icons/sinh.py` to pack the PNGs into headers, or append `--check` to check generated headers. The bar renderer draws every ink pixel without another checkerboard. Remaining 40 px icons retain their existing generated shapes; the renderer centres their ink bounds. Smaller icons and `MoonIcon.h` retain their previous bytes.
