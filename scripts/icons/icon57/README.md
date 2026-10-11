# 40 px icon alignment

`png/` contains the 42 source images used by `scripts/icons/sinh.py`. Each image is 1 bit and 40x40 px.

The 21 designs were approved on 2026-10-09. `-chon` contains the selected stroke-3 artwork; for `gan_day`, `cai_dat`, `ngon_ngu`, `thong_tin`, `ban_phim`, `them` and `cu_chi` it is made by `scripts/icons/chon.py` from true circles and lines (`tron.py`), so edit that script and not the PNG. `-xam` is made from it by `scripts/icons/xam.py`: the same shape with 1 lane of every stroke taken away, a solid 2 px line inside the selected ink and with exactly the selected ink box. The selected central stems of `doc` and `thong_tin` remain 4 px wide for symmetry; their grey stems are 2 px, centred.

Centre the ink bounding box. Opposite white margins must differ by at most 1 px. An odd ink extent leaves the extra white pixel at the right or bottom. The ink centre is therefore 19.5 px for an even extent and 19 px for an odd extent. Selected and grey variants follow this same convention; different extent parity permits a 0.5 px centre difference.

Translate whole stroke masks of the selected art. Preserve mirror symmetry. The grey art needs no alignment of its own: it has the ink box of its selected art, so the pair always shares one centre. Do not edit `-xam` by hand: change `xam.py` (its HAND table holds the few pixels the lane rule cannot place) and run it.

`tenorchrome::drawBarTab` measures the bitmap's ink bounding box and centres it inside the selected ring or the same cell rectangle. It leaves a spare pixel at right/bottom. It does not use `freeink::Icon::opticalCenterY`, the rounded mean ink row.

The `Icon57SourceAlignment`, `Icon57X3Alignment` and `Icon57TouchAlignment` host tests enforce these rules; `Icon59RoundStraight` (`test/icon57/round.py`) enforces the circles and lines of the grey art: rings within 0,5 px of their circle, straight clock hand, keys on a grid. `Icon59GreyArt` (`test/icon57/grey.py`) enforces the grey art: same ink box, within 1 px of the selected ink, mirrored when the selected icon is, no lone pixels, lighter than the selected ink. The generator also rejects source margins outside the convention.

Run `python3 scripts/icons/sinh.py` to pack the PNGs into headers, or append `--check` to check generated headers. The bar renderer draws every ink pixel without another checkerboard. Remaining 40 px icons (`vi_tri`, `cong_cu`, `muc_luc`, `chu`) retain their existing selected shapes (stroke 4); `sinh.py` cuts their grey art out of them with `xam.py` (2 passes), and the renderer centres their ink bounds. Smaller icons and `MoonIcon.h` retain their previous bytes.
