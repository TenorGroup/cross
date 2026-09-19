# Keyboard input and render synchronization

This host integration target compiles the production KeyboardEntryActivity.cpp
and the SDK's actual FreeInkUI layouts, keyboard component and touch routing.
Boundary stubs supply input snapshots, theme metrics and a text recorder. The
RenderLock boundary uses a real nonrecursive std::mutex and rejects nesting.

Nine cases cover activity entry, insertion with string growth, UTF-8 backspace,
held Delete clearing the field, cursor movement, password visibility, touch key
activation, Back/OK/Home completion and 4,000 edits concurrent with repaint.

The first seven cases pause the production render method while it holds its
lock, then run the production state-changing entry point on a second thread.
Each edit must wait for that render to finish, then produce the expected text
or display. The baseline deterministically fails this ownership gate. A separate
ThreadSanitizer build detects the baseline's actual input/render data race.
Completion checks fail if navigation occurs while holding the lock.

```sh
cmake -S test/keyboard_sync -B /tmp/keyboard-sync \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'
cmake --build /tmp/keyboard-sync -j 3
ctest --test-dir /tmp/keyboard-sync --output-on-failure
```

Use a separate build directory and `-fsanitize=thread` for race instrumentation.
The target also supports `add_subdirectory(keyboard_sync)` in the full host suite.
The device scheduler, actual panel refresh time and physical touch/button
acceptance remain target checks. The 80 ms host barrier is a scheduling probe,
not a device latency measurement.
