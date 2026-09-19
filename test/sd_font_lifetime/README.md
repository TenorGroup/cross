# SD font lifetime regression

This target compiles the complete production SdCardFont, EpdFont, EpdFontFamily,
GfxRenderer, FontCacheManager, TextBlock and BiDi source files. The in-memory SD
boundary supplies a valid version 4 cpfont with two styles, five glyphs per style
and an optional `fi` ligature. The display boundary supplies an X3-sized RAM
framebuffer. The built-in compressed-font boundary is unused by these SD paths.

Six scenarios cover:

- Ruby base text `fi`, resolved to the BOLD SD style with a ligature.
- Ruby annotation `fi`, resolved through SUP to the REGULAR SD style with a ligature.
- Twenty-five cache release-twice, scan, prewarm, draw and reload cycles.
- A short read while reloading the ligature table, then successful retry.
- A failed seek while reloading the ligature table, then successful retry.
- A failed full font reload while an existing EpdFont is still exposed, then recovery.

The ruby tests first prove the selected font/style and ligature width. They then
release caches through the real FontCacheManager, create its real PrewarmScope,
and render a real TextBlock before prewarming. This reaches the affected scan-time
measurement. After prewarming, the draw pass must change pixels in the framebuffer.
No glyph lookup or renderer method is replaced by a test predicate.

Run standalone with AddressSanitizer:

```sh
cmake -S test/sd_font_lifetime -B /tmp/sd-font-lifetime -DSD_FONT_ASAN=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/sd-font-lifetime -j 4
ctest --test-dir /tmp/sd-font-lifetime --output-on-failure
```

To repeat the red test without editing the checkout, set
`-DSD_FONT_SOURCE=/absolute/path/to/SdCardFont-before.cpp` in a separate build directory.
The same directory can also be registered with `add_subdirectory(sd_font_lifetime)`
in the parent host suite. ASan remains opt-in for that integration.

This is a host integration test with a synthetic SD font. Physical SD latency,
X3 dictionary UI navigation and target heap/stack measurements remain device gates.
