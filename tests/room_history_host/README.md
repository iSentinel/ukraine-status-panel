# Room-history checks on the server

The native test compiles the actual production room_history.cpp.
Small isolated headers replace only the ESPHome lifecycle, logging and
allocation calls. Tests use generated readings only in this host executable;
they never publish measurements to the physical panel.

Run python3 tests/room-history-native.py --help for options.
The normal run checks numeric boundaries, invalid values, gap markers,
same-bucket replacement, time guards, ring overwrite and allocation failures.
Memory and undefined-behavior checks are enabled.
The --probe option records current conversion behavior before/after a fix.

The allocation fixture frees its recorded blocks between component lifetimes. Firmware keeps its
single component for the lifetime of the device.
