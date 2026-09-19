# Settings RAM regression

This standalone host suite compiles the current production SettingsList, MenuFavorites, CrossPointSettings, ReaderFontSizes and I18n sources. It mechanically extracts the unchanged source bodies for SettingsActivity::settingValueText and the Home favorites consumer block so unrelated activity constructors/render tasks need not be linked. Manifest hashes identify both source files and excerpts.

Run `python3 run.py --repo /absolute/path/to/repo --work /absolute/path/to/isolated/build --cmake /path/to/cmake`. Requires existing project ArduinoJson dependency and host compiler; there is no network fetch or PlatformIO build. The local `CMakeLists.txt` registers this suite with CTest.

Each cold catalog test runs in a separate process. All eight FRONTLIGHT/WARMLIGHT/TOUCH preprocessor combinations are compiled; these are synthetic capability coverage, not device builds. Both IMU presence values are tested. Static/dynamic favorite labels, the actual Home consumer including file-only pins, dynamic label ownership, and warm JSON save/load allocations run for all-off and all-on capabilities.

Allocation tracking reports requested C++ new bytes. It excludes malloc, allocator bookkeeping, target heap fragmentation and target ABI. The sensor presence, clock presence, credential storage, filesystem family discovery, file-pin path resolution and unused secondary formatters are boundary doubles. File-pin fixture is ASCII. Renderer, actual SD access and button timing require simulator/device acceptance separately.

The original audit-fix evidence preserves baseline/current RED/GREEN comparisons. The actual SettingsActivity category consumer is covered by the category suite in this directory.
