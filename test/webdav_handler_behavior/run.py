#!/usr/bin/env python3
"""Compile the production WebDAV GET and mutation methods against fault-injected I/O."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile


def method(source, signature):
    start = source.index(signature)
    end = source.index("\n}", start) + 2
    return source[start:end]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path)
    parser.add_argument("--replacement", type=Path)
    parser.add_argument("--build", type=Path)
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    root = here.parent.parent
    source = (args.source or root / "src/network/WebDAVHandler.cpp").read_text()
    replacement = (args.replacement or root / "src/network/WebDavReplace.h").resolve()
    functions = "\n\n".join(method(source, signature) for signature in [
        "void WebDAVHandler::raw(", "void WebDAVHandler::handlePut(",
        "void WebDAVHandler::handleMove(", "void WebDAVHandler::handleCopy(",
        "void WebDAVHandler::handleGet(", "void WebDAVHandler::handleDelete(",
    ])
    cache_source = (root / "src/util/BookCacheUtils.cpp").read_text()
    cache_signature = "bool clearBookCache(" if "bool clearBookCache(" in cache_source else "void clearBookCache("
    cache = method(cache_source, cache_signature)
    build = args.build or Path(tempfile.mkdtemp(prefix="webdav-handler-"))
    build.mkdir(parents=True, exist_ok=True)
    generated = (here / "harness.cpp.in").read_text().replace("@REPLACEMENT@", str(replacement))
    generated = generated.replace("@PRODUCTION_CACHE@", cache).replace("@PRODUCTION_METHODS@", functions)
    (build / "test.cpp").write_text(generated)
    (build / "source-hashes.json").write_text(json.dumps({
        "WebDAVHandler.cpp": hashlib.sha256(source.encode()).hexdigest(),
        "WebDavReplace.h": hashlib.sha256(replacement.read_bytes()).hexdigest(),
        "BookCacheUtils.cpp": hashlib.sha256(cache_source.encode()).hexdigest(),
        "compiled-harness": hashlib.sha256(generated.encode()).hexdigest(),
    }, indent=2) + "\n")
    subprocess.run(["c++", "-std=c++17", "-O1", "-g", "-fsanitize=address,undefined",
                    "-fno-omit-frame-pointer", "-I", str(root / "freeink-sdk/libs/hardware/SDCardManager/include"),
                    str(build / "test.cpp"), "-o", str(build / "test")], check=True)
    return subprocess.run([str(build / "test")]).returncode


if __name__ == "__main__":
    raise SystemExit(main())
