# Shared strings in static Web checks

web_ui_static_source.py expands simple uppercase constants containing
double-quoted strings. Backup, restore and first-run checks can therefore
inspect the same field names whether the code uses literals or constants.
Quoted identifiers remain literal text. No JavaScript is executed.

Run web-ui-static-source-native.py with the project QA Python.
The regression runner executes it before the backup allowlist checks.
Behavior remains covered separately by the Chromium backup roundtrip test.
