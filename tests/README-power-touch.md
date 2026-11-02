# Power LCD control verification

The source of geometry is the canonical YAML, read without resolving secrets.
power-touch-layout.py checks3 buttons >=48x48, containment and gaps8/10.
power-touch-lvgl-native.py uses real LVGL9.5.0 and the production navigation
controller on page3:30 center/edge taps,6 outside points,3 button drags
that cancel clicks, and3 background swipes. Firmware actions are not invoked.

power-touch-width-generated.py --help: selects actual generated main.cpp
and a new JSON output. It compiles the actual refresh text lambda for UA/RU/EN
and measures5 strings using generated glyphs/line heights. Run before build
and on the actual built source. --output and native --build-dir refuse reuse.

Physical finger acceptance remains separate. production-readonly-live-probe.py
now captures the non-secret Outage Group; the release must preserve it along
with other settings. No custom actions or synthetic source messages are sent.
