# Settings font download memory regression

Run `python3 test/settings_font_memory/run.py --repo . --output /tmp/settings-font-memory --sanitize`.

The font store opens from Connections (the File Transfer hub), whose entry has already released the settings
catalog and the SD font caches; Settings no longer lists it (v1.0.69). This harness compiles the unchanged
post-Wi-Fi callback of FontDownloadActivity and CrossPointSettings.cpp. It checks every row keeps its order,
keys, enum values and persistence fields after rebuilding, byte-equivalent JSON roundtrips, owned descriptor/web
copies, dynamic callbacks, and settings persistence that rebuilds the catalog during Wi-Fi setup. The real font
callback must release it under RenderLock before fetching the manifest. Cancel and manifest failure are covered.

Use `--catalog-release-noop` to replace only the generated release body with a no-op: the same compiled test
must fail allocation and post-Wi-Fi assertions.
