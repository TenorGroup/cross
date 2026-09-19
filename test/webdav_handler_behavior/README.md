# WebDAV GET, commit and cache regression

`python3 test/webdav_handler_behavior/run.py` extracts the current production
GET, PUT, COPY and MOVE methods plus `clearBookCache` dispatch into a standalone
ASan/UBSan harness. HTTP request and filesystem boundaries are fakes with explicit
short read/write, sync, close, rename and cleanup faults. The replacement and
recovery helpers are included directly from production headers.

The scenarios cache a destination book, replace it, then reopen it and compare
content identity. They cover EPUB, TXT and XTC dispatch, successful commit,
rollback, backup cleanup, same-path no-op, Overwrite F, incomplete COPY, positive
short reads, empty and multichunk files, PUT close failure and backup-only recovery.
Postcommit cache-deletion failures exercise all three book types and PUT/COPY/MOVE.
They require HTTP 500 with an explicit committed-file explanation, committed bytes
retained, and independent attempts to invalidate both paths after MOVE.

GET reproduces the Arduino NetworkClient overload set, including its one-byte
and Stream overloads. A HalFile has an implicit bool conversion and is not a
Stream, so `write(file)` produces one byte. The regression checks complete binary
responses, empty and multichunk files, partial SD reads and socket writes, signed
read errors, early EOF, invalid counts, disconnect, close failure and allocation
failure before success headers. Incomplete responses must close their socket.

The harness proves handler ordering and the cache invalidation contract. It does
not parse real EPUB/TXT/XTC content or exercise physical SD/HTTP transport. Full
simulator and hardware journeys remain integration acceptance gates.

For baseline RED runs, pass `--source <old-WebDAVHandler.cpp>` and optionally
`--replacement <old-WebDavReplace.h>`. `--build` retains generated source/binary.
