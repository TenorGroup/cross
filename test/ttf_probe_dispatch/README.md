# TTF diagnostic dispatch regression

Run `python3 test/ttf_probe_dispatch/test_dispatch.py` from the repo. The test compiles the production TTF command body with probe spies. It checks one worker call, zero direct calls on the loop task and exactly one result for success, failure and malformed input. It does not raster fonts or model ESP32 stack usage. A host C++20 compiler is required; override it with `CXX`.

The firmware serial command is compiled only with `-DTENOR_TTF_PROBE`. Normal release and UI acceptance profiles omit this diagnostic to avoid linking the unused FreeType rasteriser. The reading pipeline is unchanged.
