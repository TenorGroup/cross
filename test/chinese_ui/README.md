# Chinese UI checks

Run `python3 scripts/check_chinese_ui.py` and `python3 test/chinese_ui/test_wrap.py`. The first also runs during ordinary PlatformIO builds. The second extracts the actual GfxRenderer::wrappedText method and executes it with deterministic width measurement; simulator tests must additionally check actual font layout.

The first check reads the five 1-bit UI faces (Geist 8, 10 and 12) and requires every character of the Chinese translation, the charset fingerprint, and CJK glyphs inside the line metrics.

Regenerate every UI face (Geist 8 to 16 with FreeType autohint, plus the Chinese subset) offline using fontTools and freetype-py: `python scripts/build_ui_fonts.py --cjk-source 'test/fonts-cjk/NotoSansSC[wght].ttf' --output /tmp/ui-fonts --install`. Source must match SHA256 a3041811a78c361b1de50f953c805e0244951c21c5bd412f7232ef0d899af0da (official google/fonts NotoSansSC variable TTF). `--install` copies the headers into lib/EpdFont/builtinFonts and re-pools the metadata. Copyright and OFL are in NotoSansSC-OFL.txt. Fonts are static 400/500 instances. There is no Arabic or Hebrew in the UI faces.

Physical contrast/ghosting and native-speaker translation review remain separate release acceptance work.
