#!/usr/bin/env python3
"""Blocks of 4 KB and up that a build step takes on a low heap go through the heap floor's door.

The door (lib/Memory/HeapFloor.h) cannot be observed from a host test, so this reads the sources: each
site below must take its block through heapfloor and must not fall back to a plain allocation.
"""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def text(path):
    return (ROOT / path).read_text()


class DoorSites(unittest.TestCase):
    def test_protected_book_is_taken_through_the_door(self):
        # One 4,096 B block, taken after the container is indexed and before the key is read.
        src = text("lib/Epub/ContentProtection.cpp")
        self.assertRegex(src, r"heapfloor::make<ProtectedBook>\(")
        self.assertNotRegex(src, r"makeUniqueNoThrow<ProtectedBook>")
        self.assertIn("#include <HeapFloor.h>", src)
        # The refusal reads like any other failed allocation to the caller.
        after = src[src.index("heapfloor::make<ProtectedBook>"):]
        self.assertRegex(after[:200], r'err = "out of memory"')

    def test_opds2_parser_block_is_taken_through_the_door(self):
        src = text("src/activities/browser/OpdsBookBrowserActivity.cpp")
        self.assertRegex(src, r"setBlockAllocator\(.*heapfloor::take\(")
        self.assertRegex(src, r"allocationFailed\(\) \? StrId::STR_MEMORY_ERROR")

    def test_details_activity_is_taken_through_the_door(self):
        # DetailsActivity carries a PublicationPageProps: 4,160 B in all, a block from 4 KB up. Both screens that
        # open it (an OPDS book, a plugin item) take it through the door and fall back when it says no.
        for path in ("src/activities/browser/OpdsBookBrowserActivity.cpp",
                     "src/activities/plugins/PluginCatalogActivity.cpp"):
            src = text(path)
            self.assertRegex(src, r"heapfloor::make<DetailsActivity>\(", path)
            self.assertNotRegex(src, r"makeUniqueNoThrow<DetailsActivity>", path)
            self.assertIn("#include <HeapFloor.h>", src, path)


if __name__ == "__main__":
    unittest.main(verbosity=2)
