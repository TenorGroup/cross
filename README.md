# tenor/cross

E-reader firmware for Xteink X3 and X4, based on [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) and [FreeInk SDK](https://github.com/Free-Ink/freeink-sdk).

Version 1.0.1 adds Vietnamese navigation, Simplified Chinese interface text, reading statistics, quotations, custom fonts and chapter search. The published X3/X4 firmware and installation guidance are available from [cross.tenor.vn](https://cross.tenor.vn/) and [release v1.0.1](https://github.com/TenorGroup/cross-releases/releases/tag/v1.0.1). Other device profiles in the source remain experimental for this release.

## Build

Install Git, Python 3.10-3.14 (validated with 3.12) and a C/C++ compiler for host tests. Clone the SDK and its nested icon submodule with the application:

```sh
git clone --recursive https://github.com/TenorGroup/cross.git
cd cross
python3 -m venv .venv
. .venv/bin/activate
python -m pip install platformio==6.1.19
pio run -e gh_release
```

The application image is written to `.pio/build/gh_release/firmware.bin`. This command builds only. Device flashing is a separate operation described on the download page. Build scripts download pinned platform packages and FreeType sources; the first build needs internet access and several gigabytes of free space. The firmware rebuilds its Arduino core with the settings in `platformio.ini`.

Optional image/font regeneration tools have extra Python requirements in `requirements.txt`. Ordinary firmware builds use the supplied bitmap-font headers. Chinese glyph coverage is checked before compilation. On the first custom-core build, the platform may replace SCons while a parent process is still using it. If it exits with `SCons.Tool.FortranCommon`, rerun `pio run -e gh_release` once after that installation completes.

Local settings belong in `platformio.local.ini`, which is ignored.

For an isolated dependency installation, set `PLATFORMIO_CORE_DIR` to an empty writable directory before running PlatformIO. This also isolates the platform patches applied by the build scripts.

## Host tests

With CMake 3.16 or newer and a C++20 compiler installed:

```sh
python scripts/gen_i18n.py lib/I18n/translations lib/I18n/
cmake -S test -B build/host -DCMAKE_BUILD_TYPE=Release
cmake --build build/host --parallel
ctest --test-dir build/host --output-on-failure
```

Run the firmware build first to resolve ArduinoJson and other shared headers. CMake fetches GoogleTest v1.17.0. Test fixtures are synthetic. Hyphenation examples use an independently selected word list and Pyphen annotations; they measure the included examples, rather than a large literary corpus.

## Licences and attribution

Application code is distributed under the [MIT licence](LICENSE), preserving the copyright of Dave Allie and the tenor/cross modifications by Tuan Q. Nguyen, tenor group jsc. FreeInk SDK retains its MIT licence and upstream notices. Dependencies and fonts retain their own licences. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) before redistributing a compiled firmware image.

## Documentation

- [User guide inherited from CrossPoint](USER_GUIDE.md), with upstream features and device support that may differ from this release.
- [Dictionary format](docs/dictionary.md).
- [Web file transfer](docs/webserver.md).
- [File formats](docs/file-formats.md).

## Source provenance

The release source was recovered against the recorded v1.0.1 file hashes. Public sanitation removes private operational material, local machine paths and fixtures without clear redistribution provenance. Third-party copyright notices are retained. Source and dependency changes made for portability are documented in [SOURCE_CHANGES.md](SOURCE_CHANGES.md). A rebuilt firmware is a new build; byte identity with the published image must be measured separately.
