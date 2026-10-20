"""The X3's lwIP send buffer: two segments per socket, so two browser tabs fit the heap the web server has."""
import configparser
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


class C3SendBuffer(unittest.TestCase):
    def test_two_segments_per_socket_on_the_c3_only(self):
        ini = configparser.ConfigParser(interpolation=None, inline_comment_prefixes=(";",))
        ini.read(ROOT / "platformio.ini")
        c3 = ini["firmware_tuned_c3"]["custom_sdkconfig"].split()
        # 2 x CONFIG_LWIP_TCP_MSS (1,436): the default 5,744 B per socket starved two tabs at once (v1.0.68, b3/f1).
        self.assertIn("CONFIG_LWIP_TCP_SND_BUF_DEFAULT=2872", c3)
        # The S3 units keep the default: their heap is not short.
        self.assertNotIn("CONFIG_LWIP_TCP_SND_BUF_DEFAULT", ini["firmware_tuned"]["custom_sdkconfig"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
