# wolfSSL source provenance

Upstream: https://github.com/wolfSSL/wolfssl
Tag: v5.9.2-stable.
Commit: ac01707f552c611fbd135cc723b2682b3e7f80f2

Generated with upstream IDE/ARDUINO/wolfssl-arduino.sh, with no INSTALL argument. All generated source, headers and license files are retained. This directory is versioned so a recursive source checkout has the exact dependency used to build.

Local integration changes:

- src/user_settings.h uses the prior Arduino 5.7.2 configuration, plus the compatibility block managed by scripts/patch_wolfssl.py. This preserves the measured embedded configuration when changing the library version.
- library.json gives PlatformIO the version and GPL-3.0-or-later license and excludes wolfssl-arduino.cpp. The firmware already implements the Arduino serial hook; compiling both creates a duplicate symbol.
- RSA SP, RSA-4096, ECC SP, TLS 1.3 and the other firmware flags live in platformio.ini. No cryptographic implementation is patched locally.

COPYING and LICENSING are upstream notices. This dependency is not covered by the application MIT license. The application and SDK keep their original notices; distribution of linked firmware must meet the dependency license terms and provide corresponding source.
