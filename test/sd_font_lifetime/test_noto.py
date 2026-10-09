import os
from pathlib import Path
import subprocess
import sys
import tempfile
import zipfile

package = os.environ.get('CROSSPOINT_NOTO_PACK')
if not package:
    print('SKIP: set CROSSPOINT_NOTO_PACK for the producer package')
    raise SystemExit(77)
with zipfile.ZipFile(package) as archive, tempfile.TemporaryDirectory() as directory:
    for entry in ('NotoSerif_14.cpfont', 'weight-6/NotoSerif_14.cpfont'):
        fixture = Path(directory) / 'font.cpfont'
        fixture.write_bytes(archive.read(entry))
        subprocess.run([sys.argv[1], str(fixture)], check=True)
