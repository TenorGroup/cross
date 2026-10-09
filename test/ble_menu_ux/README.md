# Bluetooth menu UX regression

Run `python3 test/ble_menu_ux/run.py . /tmp/ble-menu-ux` from the repository root.

The runner compiles current production Bluetooth row building, value refresh, navigation,
activation, popup callback, toggle and scan methods with fake settings, radio and UI
boundaries. ReaderMenuLayout.cpp is compiled directly. The Bluetooth case from the EPUB
menu switch executes against a child-activity/return boundary with a retained reader
position. Generated code, source hashes, compiler command and logs are saved in the output
directory. This suite covers 0, 1 and 4 bonds with opt-in enabled/disabled, exact selected
address dispatch, disabled status/empty rows, physical focus and menu action compatibility.

This projection does not run ActivityManager, render a framebuffer, connect NimBLE or
measure reader memory. Full simulator navigation and device reconnect remain separate
acceptance gates. Paired popup checks cover A linked while B is selected, disconnecting
the linked row, and preserving A's chosen address/name and save count when B never links.
The module runtime suite separately verifies candidate promotion through service().
