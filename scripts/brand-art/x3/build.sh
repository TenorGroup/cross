#!/bin/bash
set -euo pipefail
ART=$(cd "$(dirname "$0")" && pwd)
SOURCE=$(cd "$ART/../../.." && pwd)
export PYTHONDONTWRITEBYTECODE=1
export TMPDIR="${TMPDIR:-$ART/output/tmp}"
mkdir -p "$TMPDIR"
cd "$ART"
python3 render.py
python3 "$SOURCE/scripts/pack_x3_brand.py" --source-dir "$ART/bmp" --output "$SOURCE/src/components/X3BrandAssets.h"
python3 "$SOURCE/scripts/sinh_man_ngu.py" > "$TMPDIR/ManNguTenor.h"
mv "$TMPDIR/ManNguTenor.h" "$SOURCE/src/components/ManNguTenor.h"
