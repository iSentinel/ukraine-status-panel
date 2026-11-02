# Aggregate room measurement history

This component is the bounded storage backend for later week/month/year room
climate views. It does not replace the existing 24-hour five-minute history.

Storage is activated lazily only after the first valid SHT room sample or after
a validated persisted page is restored. A device that has never seen a valid
room sensor reading remains dormant: no 27 KiB PSRAM allocation and no
aggregate-history NVS writes. Once activated, invalid readings are retained as
explicit gaps rather than interpolated values.

The in-memory tiers contain 768 local-hour aggregates and 372 local-day
aggregates. Persistence uses 32 hourly pages of 24 points and 13 daily pages of
31 points. Pages carry magic/version/page-id/checksum metadata plus the last finalized raw
five-minute bucket. Version 2 reads version-1 pages under the same NVS keys; the
last legacy hour/day is kept intact and frozen until the next unambiguous bucket,
so migration never clears or double-counts old history. Pages are written
at most on page rotation, a six-hour checkpoint, shutdown, or bounded retry
after a failed sync. Large storage has no internal-heap fallback.

Local day ordinals are derived from `year + day_of_year`; local hour keys keep
spring DST gaps visible and merge the repeated fall hour into one aggregate.
Each point stores min/max/sum/count plus an explicit gap count. The backend does
not claim real sensor accuracy; physical SHT41/SHT45 acceptance remains a
hardware task.
