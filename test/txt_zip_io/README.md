# TXT and ZIP I/O regression

`run.py` extracts unchanged production method bodies from Txt.cpp, TxtToHtml.cpp and ZipFile.cpp and compiles them with the real Memory.h, Serialization.h and SDK RecoverableFile.h. Generated source and SHA-256 provenance are retained in the output directory.

TXT books open through the EPUB pipeline (TxtToHtml), so the TXT reader, its page index and the byte reader Txt::readContent are gone with their cases; what is covered of a TXT book is its conversion to HTML, Txt::streamTxtToHtml and TxtToHtml::stream (failed reads fail the conversion, a source that arrives in pieces comes out whole, a refused output fails). Txt::streamTxtToHtml compares the bytes it was handed with the file size, so a read that ends early with 0 fails the conversion instead of caching a cut book. ZIP exercises the production readFileToStream method. HalFile, Storage, renderer metrics, ZIP catalog lookup and InflateStream are deterministic boundaries. The compressed error test injects a decoder Error with produced bytes; full decompression is outside this suite. The renderer does not draw physical pixels and no SD controller or power-loss behavior is emulated.

CHECK failures throw regardless of NDEBUG. The harness compiles with -O2 -DNDEBUG so Release builds retain all checks. Allocation probes intercept new and nothrow array new; chunk malloc has a separate deterministic failure gate.

Run directly:

```sh
python3 test/txt_zip_io/run.py --output /tmp/tenor-txt-zip-io
python3 test/txt_zip_io/run.py --output /tmp/tenor-txt-zip-io-asan --sanitize
```

The local CMakeLists.txt registers `txt_zip_io_integrity`. The parent test project must add_subdirectory(txt_zip_io).

The --replay option can compile a previously frozen production-slices.cpp against compatible cases. Frozen source is evidence only; current production always comes from --source when --replay is absent.
