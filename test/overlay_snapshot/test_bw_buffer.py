import argparse
import hashlib
import json
import subprocess
import tempfile
from pathlib import Path


repo = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument("--out", type=Path, required=True)
out = parser.parse_args().out
out.mkdir(parents=True, exist_ok=True)
renderer = repo / "lib/GfxRenderer/GfxRenderer.cpp"
header = renderer.with_suffix(".h")
original = renderer.read_text()
source_hashes = {str(path.relative_to(repo)): hashlib.sha256(path.read_bytes()).hexdigest()
                 for path in (renderer, header)}
allocation_header = """#include <cstdlib>
extern "C" void* snapshotMalloc(size_t);
extern "C" void snapshotFree(void*);
#define malloc snapshotMalloc
#define free snapshotFree
"""
harness = r"""#include <GfxRenderer.h>
#include <SdCardFont.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
namespace {
std::array<uint8_t, HalDisplay::BUFFER_SIZE> pixels, original;
struct Slot { void* pointer = nullptr; size_t bytes = 0; } slots[64];
size_t calls = 0, failAt = 0, cleanups = 0, maxRequest = 0, lastRequest = 0;
const uint8_t* cleanupFrame = nullptr;
size_t liveBytes() {
  size_t total = 0;
  for (const auto& slot : slots) total += slot.bytes;
  return total;
}
}
extern "C" void* snapshotMalloc(size_t bytes) {
  maxRequest = std::max(maxRequest, bytes); lastRequest = bytes;
  if (++calls == failAt) return nullptr;
  void* pointer = std::malloc(bytes);
  assert(pointer);
  for (auto& slot : slots) if (!slot.pointer) { slot = {pointer, bytes}; return pointer; }
  assert(false); return nullptr;
}
extern "C" void snapshotFree(void* pointer) {
  for (auto& slot : slots) if (slot.pointer == pointer) {
    std::free(pointer); slot = {}; return;
  }
  assert(false);
}
HalDisplay::HalDisplay()=default;
HalDisplay::~HalDisplay()=default;
uint8_t* HalDisplay::getFrameBuffer() const { return pixels.data(); }
uint16_t HalDisplay::getDisplayWidth() const { return DISPLAY_WIDTH; }
uint16_t HalDisplay::getDisplayHeight() const { return DISPLAY_HEIGHT; }
uint16_t HalDisplay::getDisplayWidthBytes() const { return DISPLAY_WIDTH_BYTES; }
uint32_t HalDisplay::getBufferSize() const { return BUFFER_SIZE; }
void HalDisplay::cleanupGrayscaleBuffers(const uint8_t* frame) { ++cleanups; cleanupFrame=frame; }
void SdCardFont::clearCache() {}
int main() {
  static_assert(HalDisplay::BUFFER_SIZE == 52272);
  for (size_t i=0; i<original.size(); ++i) original[i]=(i*37+i/103)&255;
  HalDisplay display;
  size_t chunks=0;
  for (bool resync : {false, true}) {
    GfxRenderer r(display); r.begin(); r.redriveNextRefresh();
    pixels=original; calls=failAt=cleanups=0;
    assert(r.storeBwBuffer() && liveBytes()==original.size()); chunks=calls;
    assert(chunks==(original.size()+maxRequest-1)/maxRequest);
    assert(lastRequest==original.size()-(chunks-1)*maxRequest);
    std::fill(pixels.begin(),pixels.end(),0x42); r.restoreBwBuffer(resync);
    assert(pixels==original && liveBytes()==0 && cleanups==(resync ? 1 : 0));
    assert(r.panelFrameKnown()==resync);
    if (resync) assert(cleanupFrame==pixels.data());
    assert(r.storeBwBuffer()); std::fill(pixels.begin(),pixels.end(),0x6b);
    assert(r.storeBwBuffer()); std::fill(pixels.begin(),pixels.end(),0);
    r.restoreBwBuffer(false);
    assert(std::all_of(pixels.begin(),pixels.end(),[](uint8_t b){return b==0x6b;}));
    assert(liveBytes()==0);
    assert(r.storeBwBuffer()); r.discardStoredBwBuffer(); assert(liveBytes()==0);
  }
  for (size_t failure=1; failure<=chunks; ++failure) for (bool overwrite : {false,true}) {
    GfxRenderer r(display); r.begin(); r.redriveNextRefresh();
    pixels=original; calls=failAt=cleanups=0;
    if (overwrite) assert(r.storeBwBuffer());
    calls=0; failAt=failure;
    assert(!r.storeBwBuffer() && liveBytes()==0);
    r.restoreBwBuffer(false);
    assert(pixels==original && cleanups==0 && !r.panelFrameKnown() && liveBytes()==0);
    failAt=0; assert(r.storeBwBuffer()); r.discardStoredBwBuffer(); assert(liveBytes()==0);
  }
  calls=failAt=0;
  { GfxRenderer r(display); r.begin(); assert(r.storeBwBuffer()); }
  assert(liveBytes()==0);
  std::printf("frame=52272 chunks=%zu max_request=%zu tail=%zu oom_positions=%zu oom_modes=2\n",
              chunks,maxRequest,lastRequest,chunks);
}
"""
cmake = """
cmake_minimum_required(VERSION 3.16)
project(BwSnapshotTests LANGUAGES CXX)
enable_testing()
set(REPO_ROOT "@REPO@")
add_subdirectory("${REPO_ROOT}/test/frontlight_gesture" frontlight)
get_target_property(sources frontlight_renderer SOURCES)
list(REMOVE_AT sources 0)
list(REMOVE_ITEM sources "${REPO_ROOT}/lib/GfxRenderer/GfxRenderer.cpp")
add_executable(bw_snapshot test.cpp "@RENDERER@" ${sources})
get_target_property(includes frontlight_renderer INCLUDE_DIRECTORIES)
target_include_directories(bw_snapshot PRIVATE ${includes})
target_compile_features(bw_snapshot PRIVATE cxx_std_20)
target_compile_options(bw_snapshot PRIVATE -UNDEBUG -ffunction-sections -fdata-sections -fsanitize=address,undefined)
set_source_files_properties("@RENDERER@" PROPERTIES COMPILE_OPTIONS "-include;@ALLOC_HEADER@")
target_link_options(bw_snapshot PRIVATE -Wl,-dead_strip -fsanitize=address,undefined)
"""
variants = {
    "production": original,
    "mutation-restore-copy": original.replace(
        "    memcpy(frameBuffer + offset, bwBufferChunks[i], chunkSize);",
        "    (void)offset; (void)chunkSize;"),
    "mutation-oom-cleanup": original.replace(
        "      // Free previously allocated chunks\n      freeBwBufferChunks();",
        "      // Cleanup deliberately removed for fault-injection check."),
}
results = {}
with tempfile.TemporaryDirectory(prefix="bw-renderer-") as directory:
    for name, code in variants.items():
        assert name == "production" or code != original
        build_root = Path(directory) / name
        build_root.mkdir()
        cpp = renderer if name == "production" else build_root / "GfxRenderer.cpp"
        if name != "production":
            cpp.write_text(code)
        (build_root / "test.cpp").write_text(harness)
        (build_root / "alloc.h").write_text(allocation_header)
        (build_root / "CMakeLists.txt").write_text(cmake.replace("@REPO@", str(repo))
            .replace("@RENDERER@", str(cpp)).replace("@ALLOC_HEADER@", str(build_root / "alloc.h")))
        log = []
        for command in (["cmake", "-S", str(build_root), "-B", str(build_root / "build")],
                        ["cmake", "--build", str(build_root / "build"), "--target", "bw_snapshot", "-j", "4"]):
            run = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            log.append(run.stdout)
            assert run.returncode == 0, run.stdout
        run = subprocess.run([str(build_root / "build/bw_snapshot")],
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        log.append(run.stdout)
        (out / ("bw-renderer-" + name + ".log")).write_text("\n".join(log))
        results[name] = {"returncode": run.returncode, "output": run.stdout,
                         "renderer_sha256": hashlib.sha256(code.encode()).hexdigest()}
assert results["production"]["returncode"] == 0
assert all(results[name]["returncode"] != 0 for name in variants if name != "production")
assert source_hashes == {str(path.relative_to(repo)): hashlib.sha256(path.read_bytes()).hexdigest()
                        for path in (renderer, header)}
(out / "bw-renderer-results.json").write_text(json.dumps({"results": results,
    "source_sha256": source_hashes, "source_unchanged": True,
    "full_production_renderer_compiled": True, "esp_heap_placement_tested": False}, indent=2) + "\n")
print(json.dumps(results, indent=2))
