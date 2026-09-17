# Third-party notices

The root MIT licence applies to the application code covered by that licence. It grants no replacement licence for dependencies, fonts or other separately licensed material.

## Which release these notices cover

The dependency list below describes v1.0.3. Match notices to the firmware release you downloaded; library versions changed between releases.

| Firmware release | wolfSSL version | Bundled COPYING | Source licence notice |
| --- | --- | --- | --- |
| v1.0.1 | Arduino-wolfSSL 5.7.2 | GNU GPL version 2 | GPL version 2 or any later version, or a separate commercial licence |
| v1.0.2 | wolfSSL 5.9.2 | GNU GPL version 3 | GPL version 3 or any later version, or a separate commercial licence |
| v1.0.3 | wolfSSL 5.9.2 | GNU GPL version 3 | GPL version 3 or any later version, or a separate commercial licence |

For v1.0.1, see the [licence archive supplied with that release](https://github.com/TenorGroup/cross-releases/releases/download/v1.0.1/tenor-cross-v1.0.1-licenses.zip). For v1.0.2 and v1.0.3, see [COPYING](third_party/wolfssl/COPYING), [LICENSING](third_party/wolfssl/LICENSING) and [source provenance](third_party/wolfssl/PROVENANCE.md). The historical GPL v2 COPYING is retained at `licenses/historical/v1.0.1/Arduino-wolfSSL/COPYING` for its original dependency version. The current PlatformIO dependency is also named `Arduino-wolfSSL`; its 5.9.2 COPYING and LICENSING are retained verbatim in both `third_party/wolfssl/` and `licenses/dependencies/Arduino-wolfSSL/`.

## Dependencies in v1.0.3

- CrossPoint Reader: copyright Dave Allie and contributors, MIT. Original notices in source files remain.
- FreeInk SDK: MIT, copyright FreeInk. The SDK NOTICE preserves the Open X4 E-Paper Contributors attribution. Lucide icons retain their ISC and MIT notices in the nested submodule.
- wolfSSL 5.9.2: GPL version 3 or any later version, or a separately obtained commercial licence. The exact upstream commit, source, COPYING and LICENSING are retained in `third_party/wolfssl/`. This project provides no commercial licence grant. Linked firmware distributed using the GPL option must satisfy the GPL conditions, including complete corresponding source and build material. The application and SDK files remain available under their own MIT licences.
- BLE-enabled builds use NimBLE-Arduino 2.5.1 and its bundled Apache Mynewt NimBLE under Apache License 2.0. Their LICENSE and NOTICE files, including the bundled TinyCrypt licence, are retained in `licenses/dependencies/NimBLE-Arduino/`. The corresponding source package must include the pinned library used for the build.
- ArduinoJson and QRCode: MIT, retained in `licenses/dependencies/`.
- SdFat: retained upstream notice in `licenses/dependencies/SdFat/`.
- JPEGDEC and PNGdec: Apache License 2.0, retained in `licenses/dependencies/`.
- WebSockets: retain its LGPL licence and libb64 notices in `licenses/dependencies/WebSockets/`.
- FreeType 2.13.2: dual-licensed; the FreeType Project Licence is included with its required attribution. Portions of this software are copyright the FreeType Project (www.freetype.org). All rights reserved.
- FreeInkBook's embedded expat, miniz, pngle and hyphenation patterns retain their notices in the SDK tree.
- Noto Sans, Noto Serif, Noto Sans Arabic, Noto Sans Hebrew, Noto Sans SC, Geist, Be Vietnam Pro and OpenDyslexic: retain the supplied SIL Open Font License texts. Noto Sans SC's licence is in `licenses/fonts/`. Other family notices accompany their source fonts.
- Ubuntu fonts: retain the Ubuntu Font Licence in the font source directory.
- ESP-IDF, Arduino-ESP32 and platform toolchain components are fetched by PlatformIO and retain their package licences. Their licences also apply to redistributable portions incorporated in firmware.

The complete licence obligations of a compiled image depend on its linked components. Firmware built with the bundled wolfSSL uses the GPLv3 distribution route; the root MIT licence does not relicense those dependencies. Release source archives must include the linked sources, build scripts and configuration described in the release manifest. Historical binaries require a separate source/version match.

Official references: [wolfSSL licensing](https://www.wolfssl.com/license/), [GNU linking FAQ](https://www.gnu.org/licenses/gpl-faq.html#GPLStaticVsDynamic), [GNU GPL compatibility](https://www.gnu.org/licenses/license-list.html#apache2).

Hyphenation tries match the corresponding typst/hypher payloads after the documented four-byte header conversion. The original pattern files, including each author and licence notice, are retained under `licenses/hyphenation/`. Their licences remain separate from the application MIT licence.
