# TXT and ZIP I/O regression

`run.py` extracts unchanged production method bodies from Txt.cpp, ZipFile.cpp and TxtReaderActivity.cpp. It derives TXT state fields and method declarations from the current production header. It compiles those bodies with the real Memory.h, Serialization.h and SDK RecoverableFile.h. Generated source and SHA-256 provenance are retained in the output directory.

The exercised TXT path is initializeReader -> buildPageIndex -> loadPageAtOffset -> Txt::readContent, including cache load/save/recovery. ZIP exercises the production readFileToStream method. HalFile, Storage, renderer metrics, ZIP catalog lookup and InflateStream are deterministic boundaries. The compressed error test injects a decoder Error with produced bytes; full decompression is outside this suite. The renderer does not draw physical pixels and no SD controller or power-loss behavior is emulated.

CHECK failures throw regardless of NDEBUG. The harness compiles with -O2 -DNDEBUG so Release builds retain all checks. Allocation probes intercept new and nothrow array new; chunk malloc has a separate deterministic failure gate. Malformed cache cases inspect allocation requests and reject dangerous counts before allocation. The suite includes partial-index retry and previously committed cache preservation on failed write, sync or rename.

Run directly:

```sh
python3 test/txt_zip_io/run.py --output /tmp/tenor-txt-zip-io
python3 test/txt_zip_io/run.py --output /tmp/tenor-txt-zip-io-asan --sanitize
```

The local CMakeLists.txt registers `txt_zip_io_integrity`. The parent test project must add_subdirectory(txt_zip_io).

The --replay option can compile a previously frozen production-slices.cpp against compatible cases. Frozen source is evidence only; current production always comes from --source when --replay is absent.
