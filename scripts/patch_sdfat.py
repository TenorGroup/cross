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

Idempotent: a file already carrying MARKER is left alone. A file whose text differs from what
the patch expects stops the build. Also runs standalone on one FatPartition.cpp (host tests):
    python3 scripts/patch_sdfat.py path/to/FatPartition.cpp
"""

from pathlib import Path
import sys

MARKER = "/* CrossPoint: FAT search keeps moving forward */"

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


def patch_file(path: Path) -> bool:
    text = path.read_text()
    if MARKER in text:
        return False
    for old, new in REPLACEMENTS:
        if text.count(old) != 1:
            raise SystemExit("ERROR: SdFat patch does not match %s:\n%s" % (path, old))
        text = text.replace(old, new)
    path.write_text(text)
    return True


def patch_libdeps(project_dir: Path) -> None:
    for source in project_dir.glob(".pio/libdeps/*/SdFat/src/FatLib/FatPartition.cpp"):
        if patch_file(source):
            print("Patched SdFat FAT search: %s" % source.relative_to(project_dir))


try:
    Import("env")  # noqa: F821 (SCons-injected global)
except NameError:
    for arg in sys.argv[1:]:
        patch_file(Path(arg))
else:
    patch_libdeps(Path(env.subst("$PROJECT_DIR")))  # noqa: F821
