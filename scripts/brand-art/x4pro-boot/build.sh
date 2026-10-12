#!/bin/bash
set -euo pipefail
ART=$(cd "$(dirname "$0")" && pwd)
SOURCE=$(cd "$ART/../../.." && pwd)
export PYTHONDONTWRITEBYTECODE=1
export TMPDIR="${TMPDIR:-$ART/output/tmp}"
mkdir -p "$TMPDIR"
python3 "$ART/render.py" "${1:-V3}"
python3 "$SOURCE/scripts/pack_x3_brand.py" --source-dir "$ART/output" --output "$SOURCE/src/components/X4ProBrandAssets.h" --width 480 --height 800 --namespace x4probrand --boot-file x4pro-boot.png --sleep-file "$ART/../x4pro-sleep/x4pro-sleep.png"
