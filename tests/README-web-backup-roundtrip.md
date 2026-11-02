# Safe backup roundtrip in the browser

Run `node tests/web-backup-roundtrip-browser.cjs --help` for usage.
Select readable or embedded JavaScript with --script. An optional --output
saves a new JSON report.

The test uses the real Export and file-import controls with local fixture
events only. It never clicks Restore and cannot contact a physical panel.
It checks 24 combinations: three languages, legacy --, absent group, valid
10.1, malformed groups and out-of-range subgroup values.

An old -- value means that no group was configured. Import omits that
field so it cannot overwrite the panel's current group. The original
document remains unchanged and other invalid values are rejected.
The normal and embedded scripts run through scripts/test-web-forms.sh.
