"""
PlatformIO pre-build script: SdFat's FAT cluster search, patched in every libdep copy.

SdFat looks for a free cluster by walking the FAT one entry at a time from a search start, and
freeing a cluster pulls that start back down to it. Once the freed clusters are used again, the
next allocation walks the whole run of used clusters above them: on a card with gigabytes in use
that is thousands of FAT sector reads. On the X3 it took 3,3 s for one new file of a 30-chapter
book and 4,3 s for the folder of a new book cache, right after a book's cache was deleted.

The patch keeps the search start where the last allocation left it and lets both searches wrap
to the first cluster once before giving up, so a cluster freed below the start is still found
when the rest of the card is full.

A mount started the search at the first cluster, and the X3 mounts on every wake. FAT32 keeps a
next-free hint in its FSInfo sector for this; SdFat never read or wrote it. The patch reads it at
mount and writes it back each time the search start has moved 512 clusters on, one sector
through the data cache. scripts/check_sdfat_patch.py fails a build whose ELF lacks the patch.

Idempotent: a file already carrying MARKER is left alone. A file whose text differs from what
the patch expects stops the build. Also runs standalone on one FatPartition.cpp (host tests):
    python3 scripts/patch_sdfat.py path/to/FatPartition.cpp
"""

from pathlib import Path
import sys

MARKER = "/* CrossPoint: FAT search keeps moving forward, FSInfo next-free hint */"
# The first release of this patch, without the FSInfo hint: undone before this one is applied.
MARKER_V1 = "/* CrossPoint: FAT search keeps moving forward */"

REPLACEMENTS = [
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
    # allocateCluster: past the last cluster, wrap to the first one once.
    (
        """  Cluster_t find;
  bool setStart;
  if (m_allocSearchStart < current) {""",
        """  Cluster_t find;
  bool setStart;
  Cluster_t wrapEnd = 0;  // after a wrap: where the search began
  if (m_allocSearchStart < current) {""",
    ),
    (
        """    if (find > m_lastCluster) {
      if (setStart) {
        // Can't find space, checked all clusters.
        DBG_FAIL_MACRO;
        goto fail;
      }
      find = m_allocSearchStart;
      setStart = true;
      continue;
    }
    if (find == current) {
      // Can't find space, already searched clusters after current.
      DBG_FAIL_MACRO;
      goto fail;
    }""",
        """    if (find > m_lastCluster || (setStart && !wrapEnd && find == current)) {
      if (setStart) {
        if (wrapEnd) {
          // Can't find space, checked all clusters.
          DBG_FAIL_MACRO;
          goto fail;
        }
        // Clusters freed below the search start: one more pass from the first cluster.
        wrapEnd = current > m_allocSearchStart ? current : m_allocSearchStart;
        find = 1;
        continue;
      }
      find = m_allocSearchStart;
      setStart = true;
      continue;
    }
    if (wrapEnd && find > wrapEnd) {
      // Can't find space, checked all clusters.
      DBG_FAIL_MACRO;
      goto fail;
    }
    if (find == current) {
      // Only reached after the wrap: the file's own last cluster.
      continue;
    }""",
    ),
    # allocContiguous: same wrap.
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
]


HEADER_REPLACEMENTS = [
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

V1_REPLACEMENTS = [(old, new.replace(MARKER, MARKER_V1)) for old, new in REPLACEMENTS]

REPLACEMENTS += [
    # init: start the search at FSInfo's next-free hint.
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
      if (fsi && getLe32(fsi->leadSignature) == FSINFO_LEAD_SIGNATURE &&
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
    (
        """  updateFreeClusterCount(-1);
  *next = find;""",
        """  if (setStart) {
    noteNextFree(find);
  }
  updateFreeClusterCount(-1);
  *next = find;""",
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
    (
        """//------------------------------------------------------------------------------
// find a contiguous group of clusters""",
        """//------------------------------------------------------------------------------
// Writes FSInfo's next-free hint once the search start has moved 512 clusters on (or back), so
// the next mount starts near where allocation stopped. The sector goes out with the data cache.
__attribute__((noinline)) void FatPartition::noteNextFree(Cluster_t cluster) {
  if (!m_fsInfoSector) {
    return;
  }
  const Cluster_t hint = cluster < m_lastCluster ? cluster + 1 : 2;
  if (hint >= m_fsInfoNextFree && hint - m_fsInfoNextFree < 512) {
    return;
  }
  FsInfo_t* fsi = reinterpret_cast<FsInfo_t*>(
      dataCachePrepare(m_fsInfoSector, FsCache::CACHE_FOR_WRITE));
  if (!fsi) {
    return;
  }
  setLe32(fsi->nextFree, hint);
  m_fsInfoNextFree = hint;
}
//------------------------------------------------------------------------------
// find a contiguous group of clusters""",
    ),
]


def patch_file(path: Path) -> bool:
    text = path.read_text()
    if MARKER in text:
        return False
    if MARKER_V1 in text:
        for old, new in V1_REPLACEMENTS:
            text = text.replace(new, old)
    replacements = HEADER_REPLACEMENTS if path.suffix == ".h" else REPLACEMENTS
    for old, new in replacements:
        if text.count(old) != 1:
            raise SystemExit("ERROR: SdFat patch does not match %s:\n%s" % (path, old))
        text = text.replace(old, new)
    path.write_text(text)
    return True


def patch_libdeps(project_dir: Path) -> None:
    for name in ("FatPartition.cpp", "FatPartition.h"):
        for source in project_dir.glob(".pio/libdeps/*/SdFat/src/FatLib/" + name):
            if patch_file(source):
                print("Patched SdFat FAT search: %s" % source.relative_to(project_dir))


try:
    Import("env")  # noqa: F821 (SCons-injected global)
except NameError:
    for arg in sys.argv[1:]:
        patch_file(Path(arg))
else:
    patch_libdeps(Path(env.subst("$PROJECT_DIR")))  # noqa: F821
