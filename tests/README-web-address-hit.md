# Address suggestions and action hit testing

Run `node tests/web-address-hit-browser.cjs --help` for options.
Use --script to select readable or embedded JavaScript. An optional --css
selects a proposed stylesheet, and --output saves a new JSON report.

The test serves only local fixture responses in Chromium. It opens house
suggestions after typing an address and verifies three points across the
Find group button. A normal click must then produce exactly one lookup and
display the result. It covers 320, 390, 768 and 1024 pixel windows in English,
Russian and Ukrainian. No panel, live address provider or settings are used.

The old absolute suggestion list intercepted the action in all twelve cases.
The list now occupies normal page space and empty lists are hidden.
Both JavaScript forms are checked by scripts/test-web-forms.sh.
