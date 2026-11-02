# Forecast wrapping checks

weather-forecast-wrap-generated.py measures1131 cases from29 actual WMO descriptions,
three languages and13 configured hourly/daily cards through real LVGL9.5.0.
Fonts come from actual generated ESPHome glyphs and line heights.
It also checks full rain/time/temperature labels and six-pixel daily separation.
No network/device or synthetic publications to firmware are used.

Use --help; set USP_LVGL_ROOT. --main-cpp selects the saved generated font
and --build-dir must be new. Preserve before-fail and after-pass evidence.
Run on generated-only sources and actual built main.cpp before freeze.
Reports bind each run to its own source digest; different generator invocations
may change gzip timestamps, group IDs and line directives. See release298
generated-provenance.py: normalization must preserve actual executable content.

Physical visual acceptance is separate. Hourly297 already accepted by user.
