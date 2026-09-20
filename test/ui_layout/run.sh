#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
UI_LAYOUT_BUILD=$(mktemp -d /tmp/tenor-ui-layout.XXXXXX)
trap 'rm -rf "$UI_LAYOUT_BUILD"' EXIT
for name in geometry option_window stats_layout stats_navigation; do
  c++ -std=c++20 -Isrc "test/ui_layout/$name.cpp" -o "$UI_LAYOUT_BUILD/$name"
  "$UI_LAYOUT_BUILD/$name"
done
for name in list_render keyboard_render option_render; do
  c++ -std=c++20 -Isrc -Ifreeink-sdk/libs/ui/FreeInkUI/include "test/ui_layout/$name.cpp" freeink-sdk/libs/ui/FreeInkUI/src/FreeInkUI.cpp -o "$UI_LAYOUT_BUILD/$name"
  "$UI_LAYOUT_BUILD/$name"
done
c++ -std=c++20 -Itest/ui_layout/stats_stubs -Isrc test/ui_layout/stats_render.cpp src/components/ReadingStatsView.cpp src/components/ReadingStatsFormat.cpp src/util/NgayGio.cpp -o "$UI_LAYOUT_BUILD/stats_render"
"$UI_LAYOUT_BUILD/stats_render"
python3 test/ui_layout/test_clock_guard.py
