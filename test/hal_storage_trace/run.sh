#!/bin/sh
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
source_dir=$(CDPATH= cd -- "$here/../.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -rf "$tmp_dir"' EXIT
compiler=${CXX:-c++}
for mode in off on; do
  flags=""
  if [ "$mode" = on ]; then flags="-DTENOR_UI_ACCEPTANCE"; fi
  "$compiler" -std=c++17 -Wall -Wextra -Werror -Wno-unused-private-field -DFREEINK_CAP_USB_MSC=0 $flags \
    -I"$here/stubs" -I"$source_dir/lib/hal" \
    "$source_dir/lib/hal/HalStorage.cpp" "$here/trace_test.cpp" -o "$tmp_dir/trace_test"
  "$tmp_dir/trace_test"
  echo "hal_storage_trace $mode: PASS"
done
