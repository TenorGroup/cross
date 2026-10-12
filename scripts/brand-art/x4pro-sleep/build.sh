#!/bin/bash
set -euo pipefail
ART=$(cd "$(dirname "$0")" && pwd)
export PYTHONDONTWRITEBYTECODE=1
export TMPDIR="${TMPDIR:-$ART/output/tmp}"
mkdir -p "$TMPDIR"
python3 "$ART/render.py"
# X4ProBrandAssets.h holds the boot and the sleep planes: the boot build packs both.
"$ART/../x4pro-boot/build.sh"
