# Aggregate room-history host checks

The native test compiles the actual `usp_room_aggregate_history` production
component with isolated ESPHome lifecycle, PSRAM and preferences boundaries.
It never contacts the physical panel and never publishes generated room values.

The regression covers dormant behavior with an absent SHT sensor, lazy PSRAM
allocation, newest-wins five-minute sampling, bounded 768-hour/372-day rings,
page persistence and reboot restore, corrupt-page rejection, allocation failure,
failed-sync retry, leap-day ordering and a 23-hour DST day. ASAN/UBSAN and
float-cast-overflow checks are enabled.

`.387` intentionally adds only the backend foundation. The existing 24-hour UI
remains unchanged; week/month/year presentation is a later bounded stage.
