# Room measurement history

The fixed ring keeps at most 288 samples at five-minute intervals.
Its sample storage remains 3456 bytes, preferably in PSRAM with internal
memory fallback. It does not write readings to flash.

A newer reading in the same bucket replaces the previous reading.
Invalid input records a gap, so a failed reading does not become a plausible
temperature or humidity endpoint. Temperature must fit signed centi-degree
storage (-327.68 through 327.67 Celsius); this is an encoding limit, not a
claim about a sensor's calibrated range. Humidity must be finite and in
0 through 100 percent. No rounding occurs before these checks.

Clearing the ring resets samples and counters. Missing allocation disables
recording without requiring the optional sensor or blocking other screens.
The component does not generate readings, infer pressure or estimate charge.

Run python3 tests/room-history-native.py --help on the development server.
The native test compiles this actual component with isolated lifecycle and
allocation headers, checks invalid conversion, gaps, overwrite and memory
failures under sanitizers. It does not test physical sensor accuracy.
See docs/ROOM_HISTORY.md for user-facing behavior.

Timestamp ordering: a reading older than the newest stored timestamp
is rejected even within the same five-minute bucket. Equal timestamps
may refresh the current reading; newer invalid input remains a gap.
Capacity, data layout, sampling, chart UI and flash policy are unchanged.
