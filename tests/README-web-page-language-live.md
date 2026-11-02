# Read-only page language on the physical panel

Run web-page-language-live-probe.py --help with the project QA Python.
Provide a fresh production-readonly-live-probe.py JSON file, its expected
firmware version, and a new output path. The probe uses pinned local Node,
Playwright and Chromium packages.

Authentication stays in memory and reaches Chromium through standard input.
No password is written to arguments, reports or source files. Traffic is
restricted to GET/HEAD requests to the panel origin. All other requests are
blocked before transmission. No control is clicked and no setting is submitted.

The report records page language, tab count, viewport width and request counts.
This is a live browser check, separate from UART and native API readback.
It does not claim complete accessibility compliance.

The optional --deny-storage flag blocks localStorage only in the isolated
inspection context. It never changes browser policy or preferences on the
user computer. The same GET/HEAD-only restriction still applies.
