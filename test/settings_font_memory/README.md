# Settings font download memory regression

Run `python3 test/settings_font_memory/run.py --repo . --output /tmp/settings-font-memory --sanitize`.

The harness compiles unchanged production method bodies for category rebuilding,
row rebuilding, pause/resume, and the DownloadFonts launch branch. Hardware and
child construction are boundaries. The runner records source hashes and exact
compiler command. When lifecycle overrides are absent, it compiles the inherited
Activity methods, allowing the unpatched behavior to fail the same runtime checks.

An allocator epoch tracks all allocations made by the real settings rebuild.
The assertions require every owned allocation and vector capacity to be returned
while the download child runs. They cover an unrelated child, deferred release,
navigation retention, rebuild before the result callback, the cancelled return,
dynamic dictionary callbacks, and a cleared release flag after returning.

Host byte counts use host ABI sizes. The target log in onPause reports actual
released heap and largest free block before FontDownload starts Wi-Fi.

The shared-catalog probe additionally compiles the unchanged post-Wi-Fi callback
and CrossPointSettings.cpp. It checks 72 rows with the same order, keys, enum
values and persistence fields after rebuilding, byte-equivalent JSON roundtrips,
owned descriptor/web copies, dynamic callbacks, and settings persistence that
rebuilds the catalog during Wi-Fi setup. The real font callback must release it
under RenderLock before fetching the manifest. Cancel and manifest failure are
covered. Settings resume is exercised with the shared catalog released.

Use `--catalog-release-noop` to replace only the generated release body with a
no-op: the same compiled test must fail allocation and post-Wi-Fi assertions.
