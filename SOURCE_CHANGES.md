# Source preparation for v1.0.2

The v1.0.2 working source was captured with per-file SHA-256 hashes, including uncommitted application changes and the SDK revision. It was merged into the cleaned v1.0.1 source baseline. Source preparation is separate from publishing a release or changing repository visibility.

- Export distributable source with private operational documents, books, device state and screenshots excluded.
- Preserve third-party licences, source attribution and the SDK submodule.
- Keep independent vocabulary fixtures and generate test EPUBs from original test text.
- Regenerate HTML and translations at build time. Fetch pinned FreeType sources for firmware and native targets.
- Pin the native simulator revision and normalise compiler source paths in distributable firmware.
- Retain v1.0.2 rendering, font-weight preference, TXT wrapping, Home and status-bar changes.
- Use three line-spacing choices and three paragraph-spacing choices. Migrate older stored values and invalidate incompatible page caches.
- Preserve existing font choices across upgrades, including older built-in selections whose stored family name was omitted.
- Require per-session transfer credentials, matching browser Origin and WebSocket authentication. Protect internal paths and preserve old files during WebDAV replacements.
- Verify HTTPS certificates and hostnames, block downgrade redirects and cross-origin credential forwarding, and bound HTTP headers and buffered bodies.
- Include pinned wolfSSL 5.9.2 source, with RSA SP support and per-host RSA/ECDSA trust anchors. Preserve its GPL notices separately from the application MIT licence.
- Patch the pinned simulator adapter for the same authentication contract; host tests and native tests are distinct from device TLS validation.

The application MIT licence coexists with separate dependency and font licences. See THIRD_PARTY_NOTICES.md. Generated firmware is verified separately from the source tree; binary identity across toolchains is not assumed.
