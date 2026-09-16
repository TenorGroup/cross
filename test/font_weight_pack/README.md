# Experimental outline font packs

Run `python3 -m unittest discover -s test/font_weight_pack -v` with freetype-py and fontTools installed. The raster tests cover eight sizes, unchanged advances and retained four-level glyphs. The manager test compiles the production manager and registry helpers against small I/O/renderer substitutes. It exercises equal source hashes, distinct weight IDs, unweighted UI reuse and missing/corrupt variant fallback.

Set `CROSSPOINT_TEST_MUTATE_WEIGHT_ID=1` to compile a temporary mutant that omits weight from the ID. The manager test must fail. Product source remains unchanged.

Actual file discovery and UI journeys are covered by `test/reading_stats_simulator/test_font_weight.py`.

Build a pack with the existing converter:

```sh
python3 lib/EpdFont/scripts/fontconvert_sdcard.py \
  --regular /path/Font-Regular.ttf --bold /path/Font-Bold.ttf \
  --name Example --intervals reading --trial-weights --output-dir /tmp/Example
```

The trial option defaults to 12,14,16,18,20,22,24,26 and preserves all existing style/fallback arguments. The base files stay at the family root; variants live under `weight-1` and `weight-2`. Copy the complete folder into the device's existing font root (`/.fonts` or `/fonts`). Back up existing files before replacement. `.weight-recipe.json` records source hashes, FreeType version, effective 26.6 strength and output hashes. This pack format requires the experimental loader for weight selection; the base remains compatible with the previous loader.

On the device, Text settings > Style > Weight (trial) cycles the installed levels. Missing or invalid variants fall back to the base and persist level0. Built-in fonts or SD fonts without a trial pack show that no weight pack is installed. Keep Default until physical A/B review; simulator images do not validate panel reflectance, ghosting or refresh latency.
