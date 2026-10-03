#!/bin/zsh
# run_red.sh <work-dir> [repo-root]
# Three deliberate breakages, each built as a mutated COPY of one production file (the checkout is
# never edited). Every one must turn the shed/parser test red, with AddressSanitizer on.
#   keep-glyph    the layout keeps a glyph pointer across build steps (the thing the audit found no sign of)
#   no-advance    the layout stops rebuilding the advance table before measuring (the thing a shed empties)
#   kern-residue  layout kerning reads the resident kern tables again (the latch in GfxRenderer::getKerning)
set -u
W=$1
ROOT=${2:-${0:A:h}/../..}
ROOT=${ROOT:A}
mkdir -p $W

mutant() {  # name  source-file
  local name=$1 src=$2
  mkdir -p $W/$name
  python3 - "$ROOT/$src" "$W/$name/$(basename $src)" "$name" <<'PY'
import sys
src, dst, name = sys.argv[1:4]
s = open(src).read()
if name == "keep-glyph":
    anchor = "  if (words.empty()) return true;\n"
    add = anchor + """  {
    static const EpdGlyph* heldGlyph = nullptr;
    static uint16_t heldAdvance = 0;
    if (heldGlyph && heldGlyph->advanceX != heldAdvance) return false;
    const EpdGlyph* g = renderer.getFontMap().at(fontId).getGlyph('a', EpdFontFamily::REGULAR);
    const bool resident = g && !renderer.getSdCardFonts().at(fontId)->isOverflowGlyph(g);
    heldGlyph = resident ? g : nullptr;
    heldAdvance = resident ? g->advanceX : 0;
  }
"""
    assert s.count(anchor) == 1
    s = s.replace(anchor, add)
    s = "#include <SdCardFont.h>\n" + s
elif name == "no-advance":
    anchor = "    renderer.ensureSdCardFontReady(fontId, segments.data(), segmentLens.data(), segments.size(), words.size() > 1,\n                                   hyphenationEnabled, styleMask);"
    assert s.count(anchor) == 1
    s = s.replace(anchor, "    (void)segments; (void)segmentLens;")
elif name == "kern-residue":
    anchor = "sdCardFonts_.count(fontId) ? 0 : "
    assert s.count(anchor) == 1
    s = s.replace(anchor, "")
open(dst, "w").write(s)
PY
  local define
  case $name in
    keep-glyph|no-advance) define=-DSHED_PARSED_TEXT_SOURCE=$W/$name/ParsedText.cpp ;;
    kern-residue) define=-DSHED_GFX_SOURCE=$W/$name/GfxRenderer.cpp ;;
  esac
  cmake -S $ROOT/test/shed_parser -B $W/$name/build -DCMAKE_BUILD_TYPE=Debug -DSHED_ASAN=ON $define > $W/$name/cfg.log 2>&1 || { echo "RED-SETUP-FAIL $name"; return; }
  cmake --build $W/$name/build -j6 > $W/$name/build.log 2>&1 || { echo "RED-BUILD-FAIL $name"; return; }
  local red=0
  for scenario in every-tick starve-every starve-sweep; do
    $W/$name/build/ShedParserTest $scenario > $W/$name/$scenario.log 2>&1
    if [ $? -ne 0 ]; then
      echo "RED $name: $scenario -> $(grep -m1 -E 'FAIL|ERROR: AddressSanitizer' $W/$name/$scenario.log)"
      red=1
      break
    fi
  done
  [ $red -eq 1 ] || echo "NOT-RED $name (the test cannot see this breakage)"
}

mutant keep-glyph lib/Epub/Epub/ParsedText.cpp
mutant no-advance lib/Epub/Epub/ParsedText.cpp
mutant kern-residue lib/GfxRenderer/GfxRenderer.cpp
