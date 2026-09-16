# Chinese UI checks

Run `python3 scripts/check_chinese_ui.py --baseline test/chinese_ui/font-baseline.json` and `python3 test/chinese_ui/test_wrap.py`. The first also runs during ordinary PlatformIO builds. The second extracts the actual GfxRenderer::wrappedText method and executes it with deterministic width measurement; simulator tests must additionally check actual font layout.

Baseline glyph hashes cover all previous UI glyph metrics/bitmaps and kerning arrays. Chinese glyphs are checked in the generated headers, including bounding boxes and charset fingerprint.

Regenerate fonts offline using fontTools and freetype-py: `python scripts/build_chinese_ui.py --root . --source /path/to/NotoSansSC.ttf --chinese lib/I18n/translations/chinese.yaml --output /tmp/chinese-fonts --recipe test/chinese_ui/font-baseline.json`. Source must match SHA256 a3041811a78c361b1de50f953c805e0244951c21c5bd412f7232ef0d899af0da (official google/fonts NotoSansSC variable TTF). Copy validated output headers into lib/EpdFont/builtinFonts. Copyright and OFL are in NotoSansSC-OFL.txt. Fonts are static 400/500 instances; default reader font and existing glyphs stay unchanged.

Physical contrast/ghosting and native-speaker translation review remain separate release acceptance work.
