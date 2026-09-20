"""Check the actual converter accepts the bounded monochrome storage contract."""
from pathlib import Path
import subprocess
import sys
ROOT = Path(__file__).resolve().parents[2]
run = subprocess.run([sys.executable, str(ROOT/'lib/EpdFont/scripts/fontconvert.py'), '--help'], capture_output=True, text=True)
assert run.returncode == 0, run.stderr
assert '--mono-coverage' in run.stdout, 'UI compressed monochrome storage option missing'
assert '--max-group-bytes' in run.stdout, 'Bounded decompression option missing'
print('PASS: generator interface')
