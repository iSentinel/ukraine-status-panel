# Local alarm manager

Three disabled-by-default schedules use the existing ESPHome RealTimeClock.
LCD, generic Web entities and authenticated Native API actions call configure().
No HTTP polling, remote audio, extra timer service or synthetic device time is used.

The portable alarm_schedule.h owns a fixed 32-byte record:
time, supported weekday mask, enabled flag and per-slot last firing date.
The adapter commits all simultaneous firing guards before invoking automation.
Idle polls and acknowledgements never write preferences. A failed commit suppresses
notifications and retries; a later explicit successful configuration can recover it.
A missing/corrupt record keeps defaults disabled.

UTC minutes reject boot/reacquired-time delivery and clock jumps; the local date
guards the repeated DST hour and reboot cases. Editing an already-fired schedule
preserves its date guard. Runtime slot revisions reject stale LCD drafts.

Production controls currently offer Daily127 / Weekdays62 / Weekends65.
The portable core supports arbitrary nonzero seven-bit masks for later expansion,
but the adapter rejects unsupported current UI masks.

The Native API readback action usp_qa_dump_alarms is read-only. Configuration
requires an explicit slot/time/days/enabled tuple and optional expected revision;
a negative revision means the caller explicitly uses the current server state.
No API action fabricates time or an alarm occurrence.

See docs/ALARMS.md for user behavior and tests/alarm-schedule-native.py for
AddressSanitizer/UndefinedBehaviorSanitizer checks of the real core and adapter.
Actual NVS, physical controls and buzzer acoustics require hardware evidence.
