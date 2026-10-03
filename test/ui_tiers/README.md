# UI font tier checks

The standalone CMake target runs size policy and production auxiliary-font lifecycle tests. Manager I/O and font files use bounded test doubles; the lifecycle implementation is production source.

```sh
cmake -S test/ui_tiers -B /tmp/tenor-ui-tier-tests
cmake --build /tmp/tenor-ui-tier-tests
ctest --test-dir /tmp/tenor-ui-tier-tests --output-on-failure
```

Use a Python environment with freetype-py and fontTools for regeneration. Source TTF/OFL files live under `lib/EpdFont/builtinFonts/source`; verify the pinned CJK source hash before generating.

```sh
python scripts/build_ui_fonts.py --cjk-source 'test/fonts-cjk/NotoSansSC[wght].ttf' --output /tmp/ui-fonts
python test/ui_tiers/verify_fonts.py --generated /tmp/ui-fonts
python test/ui_tiers/check_ui_faces.py
python scripts/check_chinese_ui.py
```

`check_ui_faces.py` also runs under CTest: no Arabic or Hebrew glyph in the 1-bit UI faces, every Chinese and Vietnamese character present, and identical stem widths in Geist 12.

The production decoder test uses the emitted uncompressed references and links the actual decompressor/InflateReader/uzlib implementation. Example macOS host command:

```sh
clang -c -ffunction-sections lib/uzlib/src/tinflate.c -Ilib/uzlib/src -o /tmp/tenor-ui-tinflate.o
clang++ -std=c++17 -O1 -fsanitize=address,undefined -Wl,-dead_strip -include cstdlib -Itest/ui_tiers/stubs -Ilib/EpdFont -Ilib/InflateReader -Ilib/uzlib/src -Ilib/Utf8 -I/tmp/ui-fonts test/ui_tiers/FontDecompressionTest.cpp lib/EpdFont/FontDecompressor.cpp lib/InflateReader/InflateReader.cpp lib/Utf8/Utf8.cpp /tmp/tenor-ui-tinflate.o -o /tmp/tenor-ui-decoder
/tmp/tenor-ui-decoder
```

Firmware size, target heap/latency, layout and physical panel acceptance are separate integration checks.

`UIBootAndApply` compiles the current `setupDisplayAndFonts` function body, the production `UIFontTiers.cpp`, and the actual inline `replaceBuiltinFont` method. Host adapters replace display/task/storage interfaces. It executes saved tiers 0/1/2 through boot and observes the font metrics at the first paint boundary, locking, cache invalidation, reader-map stability, and all-or-nothing preflight for missing/SD aliases. Settings-file parsing remains covered by the settings tests.

```sh
python3 test/ui_tiers/run_boot_apply.py --build /tmp/tenor-ui-boot
python3 test/ui_tiers/run_boot_apply.py --build /tmp/tenor-ui-red-boot --mutant omit-apply
python3 test/ui_tiers/run_boot_apply.py --build /tmp/tenor-ui-red-preflight --mutant omit-preflight
python3 test/ui_tiers/run_boot_apply.py --build /tmp/tenor-ui-red-cache --mutant omit-cache
```

The last three commands intentionally fail; each result records source hashes and executable output in its build directory. CTest runs the positive cases with AddressSanitizer and UndefinedBehaviorSanitizer.
