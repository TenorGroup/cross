"""
PlatformIO pre-build script: SdFat's FAT cluster search, patched in this environment's libdep.

SdFat looks for a free cluster by walking the FAT one entry at a time from a search start, and
freeing a cluster pulled that start back down to it. Once the freed clusters were used again, the
next allocation walked every used cluster above them: thousands of FAT sector reads on a card
with gigabytes in use. On the X3 that was 3,3 s for one new file of a 30-chapter book and 4,3 s
for the folder of a new book cache.

What the patch changes in FatLib/FatPartition.cpp and .h:
  * a freed cluster no longer moves the search start; both searches (one cluster, a contiguous
    run) wrap to the first cluster once, so clusters freed below the start are still found;
  * a file grows into the cluster after its last one when that is free;
  * a mount starts the search at FAT32's FSInfo next-free hint (SdFat never read it; the X3
    mounts on every wake), and the search writes the hint back once it has moved 512 clusters
    on or moved back. The FSInfo sector is written only after its three signatures check out,
    and only its next-free field changes.

scripts/check_sdfat_patch.py fails a build whose ELF lacks the patch.

Earlier versions of this patch are undone first; a source that matches neither the patch nor the
library it was written for stops the build. Files are rewritten through a temporary file.
Standalone (host tests):
    python3 scripts/patch_sdfat.py [--unpatch] FatPartition.cpp FatPartition.h
"""

import os
from pathlib import Path
import sys

MARKER = "/* CrossPoint SdFat patch 2 */"
# Version 1 (search start kept, wraps; no FSInfo hint), undone before version 2 applies.
MARKER_V1 = "/* CrossPoint: FAT search keeps moving forward */"
V1_CPP = [('    updateFreeClusterCount(1);\n    if (cluster < m_allocSearchStart) {\n      m_allocSearchStart = cluster - 1;\n    }\n    cluster = next;', '    updateFreeClusterCount(1);\n    /* CrossPoint: FAT search keeps moving forward */\n    cluster = next;'), ('  Cluster_t find;\n  bool setStart;\n  if (m_allocSearchStart < current) {', '  Cluster_t find;\n  bool setStart;\n  Cluster_t wrapEnd = 0;  // after a wrap: where the search began\n  if (m_allocSearchStart < current) {'), ("    if (find > m_lastCluster) {\n      if (setStart) {\n        // Can't find space, checked all clusters.\n        DBG_FAIL_MACRO;\n        goto fail;\n      }\n      find = m_allocSearchStart;\n      setStart = true;\n      continue;\n    }\n    if (find == current) {\n      // Can't find space, already searched clusters after current.\n      DBG_FAIL_MACRO;\n      goto fail;\n    }", "    if (find > m_lastCluster || (setStart && !wrapEnd && find == current)) {\n      if (setStart) {\n        if (wrapEnd) {\n          // Can't find space, checked all clusters.\n          DBG_FAIL_MACRO;\n          goto fail;\n        }\n        // Clusters freed below the search start: one more pass from the first cluster.\n        wrapEnd = current > m_allocSearchStart ? current : m_allocSearchStart;\n        find = 1;\n        continue;\n      }\n      find = m_allocSearchStart;\n      setStart = true;\n      continue;\n    }\n    if (wrapEnd && find > wrapEnd) {\n      // Can't find space, checked all clusters.\n      DBG_FAIL_MACRO;\n      goto fail;\n    }\n    if (find == current) {\n      // Only reached after the wrap: the file's own last cluster.\n      continue;\n    }"), ("  // search the FAT for free clusters\n  while (1) {\n    if (endCluster > m_lastCluster) {\n      // Can't find space.\n      DBG_FAIL_MACRO;\n      goto fail;\n    }", "  // search the FAT for free clusters\n  bool wrapped = false;\n  while (1) {\n    if (endCluster > m_lastCluster) {\n      if (!wrapped && m_allocSearchStart > 1) {\n        wrapped = true;\n        setStart = false;\n        endCluster = bgnCluster = 2;\n        continue;\n      }\n      // Can't find space.\n      DBG_FAIL_MACRO;\n      goto fail;\n    }")]

CPP = [
    # freeChain: a freed cluster no longer pulls the search start down.
    (
        """    updateFreeClusterCount(1);
    if (cluster < m_allocSearchStart) {
      m_allocSearchStart = cluster - 1;
    }
    cluster = next;""",
        """    updateFreeClusterCount(1);
    """ + MARKER + """
    cluster = next;""",
    ),
    # allocateCluster, whole.
    (
        "bool FatPartition::allocateCluster(Cluster_t current, Cluster_t* next) {\n  Cluster_t find;\n  bool setStart;\n  if (m_allocSearchStart < current) {\n    // Try to keep file contiguous. Start just after current cluster.\n    find = current;\n    setStart = false;\n  } else {\n    find = m_allocSearchStart;\n    setStart = true;\n  }\n  while (1) {\n    find++;\n    if (find > m_lastCluster) {\n      if (setStart) {\n        // Can't find space, checked all clusters.\n        DBG_FAIL_MACRO;\n        goto fail;\n      }\n      find = m_allocSearchStart;\n      setStart = true;\n      continue;\n    }\n    if (find == current) {\n      // Can't find space, already searched clusters after current.\n      DBG_FAIL_MACRO;\n      goto fail;\n    }\n    uint32_t f;\n    int8_t fg = fatGet(find, &f);\n    if (fg < 0) {\n      DBG_FAIL_MACRO;\n      goto fail;\n    }\n    if (fg && f == 0) {\n      break;\n    }\n  }\n  if (setStart) {\n    m_allocSearchStart = find;\n  }\n  // Mark end of chain.\n  if (!fatPutEOC(find)) {\n    DBG_FAIL_MACRO;\n    goto fail;\n  }\n  if (current) {\n    // Link clusters.\n    if (!fatPut(current, find)) {\n      DBG_FAIL_MACRO;\n      goto fail;\n    }\n  }\n  updateFreeClusterCount(-1);\n  *next = find;\n  return true;\n\nfail:\n  return false;\n}\n",
        "bool FatPartition::allocateCluster(Cluster_t current, Cluster_t* next) {\n  // A file grows into the cluster after its last one when that is free: files appended to over\n  // weeks (statistics, quotes) stay in one run. Otherwise one pass over the whole FAT from the\n  // search start, wrapping to the first cluster.\n  Cluster_t find = 0;\n  bool setStart = false;\n  if (current && current < m_lastCluster) {\n    uint32_t f;\n    int8_t fg = fatGet(current + 1, &f);\n    if (fg < 0) {\n      DBG_FAIL_MACRO;\n      goto fail;\n    }\n    if (fg && f == 0) {\n      find = current + 1;\n    }\n  }\n  if (!find) {\n    setStart = true;\n    Cluster_t candidate = m_allocSearchStart;\n    for (Cluster_t n = 1; n < m_lastCluster; n++) {\n      candidate = candidate >= m_lastCluster ? 2 : candidate + 1;\n      uint32_t f;\n      int8_t fg = fatGet(candidate, &f);\n      if (fg < 0) {\n        DBG_FAIL_MACRO;\n        goto fail;\n      }\n      if (fg && f == 0) {\n        find = candidate;\n        break;\n      }\n    }\n    if (!find) {\n      // Can't find space, checked all clusters.\n      DBG_FAIL_MACRO;\n      goto fail;\n    }\n  }\n  if (setStart) {\n    m_allocSearchStart = find;\n    noteNextFree(find);\n  }\n  // Mark end of chain.\n  if (!fatPutEOC(find)) {\n    DBG_FAIL_MACRO;\n    goto fail;\n  }\n  if (current) {\n    // Link clusters.\n    if (!fatPut(current, find)) {\n      DBG_FAIL_MACRO;\n      goto fail;\n    }\n  }\n  updateFreeClusterCount(-1);\n  *next = find;\n  return true;\n\nfail:\n  return false;\n}\n",
    ),
    # allocContiguous: wrap once, and keep the FSInfo hint.
    (
        """  // search the FAT for free clusters
  while (1) {
    if (endCluster > m_lastCluster) {
      // Can't find space.
      DBG_FAIL_MACRO;
      goto fail;
    }""",
        """  // search the FAT for free clusters
  bool wrapped = false;
  while (1) {
    if (endCluster > m_lastCluster) {
      if (!wrapped && m_allocSearchStart > 1) {
        wrapped = true;
        setStart = false;
        endCluster = bgnCluster = 2;
        continue;
      }
      // Can't find space.
      DBG_FAIL_MACRO;
      goto fail;
    }""",
    ),
    (
        """  if (setStart) {
    m_allocSearchStart = endCluster;
  }""",
        """  if (setStart) {
    m_allocSearchStart = endCluster;
    noteNextFree(endCluster);
  }""",
    ),
    # noteNextFree.
    (
        """//------------------------------------------------------------------------------
// find a contiguous group of clusters""",
        """//------------------------------------------------------------------------------
// Writes FSInfo's next-free hint once the search start has moved 512 clusters on, or moved back,
// so the next mount starts near where allocation stopped. What the data cache holds goes out
// first, the FSInfo sector is read from the card again, and it is changed only in its next-free
// field and only while its signatures hold. It goes out with the data cache.
__attribute__((noinline)) void FatPartition::noteNextFree(Cluster_t cluster) {
  if (!m_fsInfoSector) {
    return;
  }
  const Cluster_t hint = cluster < m_lastCluster ? cluster + 1 : 2;
  if (hint >= m_fsInfoNextFree && hint - m_fsInfoNextFree < 512) {
    return;
  }
  FsCache* cache = dataCache();
  if (!cache->sync()) {
    return;
  }
  // From the card, not a copy the cache may still hold.
  cache->invalidate();
  FsInfo_t* fsi = reinterpret_cast<FsInfo_t*>(
      dataCachePrepare(m_fsInfoSector, FsCache::CACHE_FOR_READ));
  if (!fsi) {
    cache->invalidate();
    return;
  }
  if (getLe32(fsi->leadSignature) != FSINFO_LEAD_SIGNATURE ||
      getLe32(fsi->structSignature) != FSINFO_STRUCT_SIGNATURE ||
      getLe32(fsi->trailSignature) != FSINFO_TRAIL_SIGNATURE) {
    m_fsInfoSector = 0;
    return;
  }
  setLe32(fsi->nextFree, hint);
  cache->dirty();
  m_fsInfoNextFree = hint;
}
//------------------------------------------------------------------------------
// find a contiguous group of clusters""",
    ),
    # init: read FSInfo's next-free hint.
    (
        """  m_cache.setMirrorOffset(m_sectorsPerFat);
#if USE_SEPARATE_FAT_CACHE
  m_fatCache.setMirrorOffset(m_sectorsPerFat);
#endif  // USE_SEPARATE_FAT_CACHE
  return true;""",
        """  // FAT32 FSInfo's next-free hint: where the last session's allocations stopped.
  m_fsInfoSector = 0;
  m_fsInfoNextFree = 0;
  if (m_fatType == 32) {
    const uint16_t fsInfo = getLe16(bpb->fat32FSInfoSector);
    if (fsInfo != 0 && fsInfo < getLe16(bpb->reservedSectorCount)) {
      const Sector_t sector = startSector + fsInfo;
      const FsInfo_t* fsi =
          reinterpret_cast<FsInfo_t*>(dataCachePrepare(sector, FsCache::CACHE_FOR_READ));
      if (!fsi) {
        m_cache.invalidate();
      } else if (getLe32(fsi->leadSignature) == FSINFO_LEAD_SIGNATURE &&
                 getLe32(fsi->structSignature) == FSINFO_STRUCT_SIGNATURE &&
                 getLe32(fsi->trailSignature) == FSINFO_TRAIL_SIGNATURE) {
        m_fsInfoSector = sector;
        const uint32_t hint = getLe32(fsi->nextFree);
        if (hint >= 3 && hint <= m_lastCluster) {
          m_allocSearchStart = hint - 1;
          m_fsInfoNextFree = hint;
        }
      }
    }
  }
  m_cache.setMirrorOffset(m_sectorsPerFat);
#if USE_SEPARATE_FAT_CACHE
  m_fatCache.setMirrorOffset(m_sectorsPerFat);
#endif  // USE_SEPARATE_FAT_CACHE
  return true;""",
    ),
]

HEADER = [
    (
        """  Cluster_t m_allocSearchStart;      // Start cluster for alloc search.
""",
        """  Cluster_t m_allocSearchStart;      // Start cluster for alloc search.
  """ + MARKER + """
  Sector_t m_fsInfoSector = 0;       // FAT32 FSInfo sector, 0 when absent.
  Cluster_t m_fsInfoNextFree = 0;    // Next-free hint last written there.
  void noteNextFree(Cluster_t cluster);
""",
    ),
]


def _swap(text, pairs, path, forward):
    for old, new in pairs:
        before, after = (old, new) if forward else (new, old)
        if text.count(before) != 1:
            raise SystemExit("ERROR: SdFat patch does not match %s:\n%s" % (path, before))
        text = text.replace(before, after)
    return text


def unpatched(text, path):
    """The library's own text, from any version of this patch."""
    header = path.suffix == ".h"
    if MARKER in text:
        text = _swap(text, HEADER if header else CPP, path, forward=False)
    elif MARKER_V1 in text and not header:
        text = _swap(text, V1_CPP, path, forward=False)
    return text


def patched(text, path):
    return _swap(unpatched(text, path), HEADER if path.suffix == ".h" else CPP, path, forward=True)


def rewrite(path, unpatch=False):
    path = Path(path)
    text = path.read_text()
    result = unpatched(text, path) if unpatch else patched(text, path)
    if result == text:
        return False
    tmp = path.with_name(path.name + ".tmp")
    tmp.write_text(result)
    os.replace(tmp, path)
    return True


def patch_environment(env):
    fatlib = Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env.subst("$PIOENV") / "SdFat" / "src" / "FatLib"
    sources = [fatlib / "FatPartition.cpp", fatlib / "FatPartition.h"]
    if not all(source.is_file() for source in sources):
        raise SystemExit("ERROR: SdFat not found at %s; it is pinned in lib_deps" % fatlib)
    for source in sources:
        if rewrite(source):
            print("Patched SdFat FAT search: %s" % source)


try:
    Import("env")  # noqa: F821 (SCons-injected global)
except NameError:
    args = sys.argv[1:]
    unpatch = "--unpatch" in args
    for arg in args:
        if arg != "--unpatch":
            rewrite(arg, unpatch)
else:
    patch_environment(env)  # noqa: F821
