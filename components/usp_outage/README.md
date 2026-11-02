# Yasno outage calendar

Use the same panel_time clock and Europe/Kyiv timezone as LCD.
This component does not set time, power, network or alarm settings.
Today and tomorrow are checked independently against local calendar dates.
Missing time, invalid or mismatched dates expose unknown status and no slots.
Same-day cached data retains the existing age/stale policy.

Supported provider dates: YYYY-MM-DD or local-midnight ISO timestamps
with +02:00/+03:00. Unknown formats remain unknown. updatedOn is not the
schedule date. Next-day arithmetic does not assume a 86400-second DST day.
A date change queues one normal worker refresh. Unknown tomorrow is --.
History is recorded only with both days valid.

Run server-only actual accessor/calendar tests with ASAN/UBSAN:
    python3 tests/outage-calendar-native.py --help
    python3 tests/outage-calendar-native.py

Tests do not change the physical panel clock. Real midnight/DST is separate
from fixtures. Read-only usp_outage_dump logs OUTAGE_CALENDAR diagnostics.
