# Source preparation for v1.0.1

The archived release manifest covers 886 application, library, generated-source and build-script files. Every original entry was recovered with its recorded SHA-256 before sanitation. SDK and test provenance comes from the separately recorded release-morning snapshot; those files were outside the release manifest.

Preparation changes:

- Export only the distributable source into new Git history, with the older private history retained separately.
- Remove project operations documents, experimental data, personal device state and screenshots.
- Replace book-derived hyphenation fixtures with independent vocabulary samples and synthetic EPUB generators.
- Remove private comments and absolute paths from font-header provenance. Preserve font data and firmware behaviour.
- Accept an input path for the optional sleep-image generator.
- Regenerate HTML and translation headers during the normal build and fetch FreeType through its pinned build script.
- Preserve application, SDK and third-party copyright notices; add missing font and dependency notices.
- Pin the cleaned SDK through its Git submodule commit.

Comment, documentation and fixture changes do not intentionally alter firmware behaviour. Regeneration, compiler versions, source locations and Git metadata can affect bytes in a new binary. Compare produced images independently before treating a rebuilt image as identical to the published release.
