#!/usr/bin/env python3
"""RAM the X3 pays at boot for features most people never use is made when the feature is first used.

v1.1.0 against v1.0.76 added 1,731 B of static RAM on the X3 (the build gate is 128 KiB of headroom). These
objects are the plugin event table (720 B), the LCP device and passphrase stores (56 + 20 B, each with a guard) and
the shared crypto backend (52 B). The firmware map is checked by hand on a build (nm --size-sort); this reads the
sources so a later change cannot put them back.
"""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def text(path):
    return (ROOT / path).read_text()


class LazyStatics(unittest.TestCase):
    def test_plugin_event_table_is_made_when_a_plugin_subscribes(self):
        src = text("src/util/PluginEvents.cpp")
        self.assertNotRegex(src, r"(?m)^Subscriber\s+subscribers\[")
        self.assertRegex(src, r"new \(std::nothrow\) Subscriber\[")

    def test_lcp_device_store_is_made_on_first_use(self):
        src = text("src/network/LcpDevice.cpp")
        self.assertNotRegex(src, r"LcpDeviceStore::getInstance\(\)")

    def test_lcp_passphrase_store_is_made_on_first_use(self):
        header, source = text("src/LcpPassphraseStore.h"), text("src/LcpPassphraseStore.cpp")
        self.assertNotIn("LCP_PASSPHRASES", header)
        self.assertRegex(header, r"static std::string get\(")
        self.assertRegex(source, r"new \(std::nothrow\) LcpPassphraseStore")

    def test_content_crypto_is_made_when_a_protected_entry_is_read(self):
        self.assertNotRegex(text("lib/Epub/ContentProtection.cpp"), r"static WolfsslCrypto")


if __name__ == "__main__":
    unittest.main(verbosity=2)
