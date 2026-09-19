# History navigation regression

This target compiles the complete production ReadingStatsStore and
BookStatsLibraryActivity. The host replaces clock, settings, rendering and the
storage boundary. Storage uses real temporary JSON files and ArduinoJson.
The persistence stub retains the read/parse/write shape used by this store;
the separate storage recovery suite owns persistence helper fault injection.

The fixture contains 100, 1,000 and 5,000 hashed per-book snapshots. The first
page and a new page still enumerate and parse the complete history. Revisiting
the adjacent page must produce identical rows with zero storage opens, reads
and parses. Requested delay milliseconds are counted without sleeping, so the
printed latency measures host CPU/filesystem work and is not an SD timing.

The integration also checks invalidation through activation, title, day, JSON
load, reset, failed load; oversize-page bypass before and after cache swaps;
empty-page actions after reset; and per-book/global backup recovery.

Platform locks/render calls are no-ops in this host test. Physical X3 timing,
heap/stack and concurrent renderer acceptance belong to the final device gate.
