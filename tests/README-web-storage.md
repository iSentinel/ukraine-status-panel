# Optional browser storage

Run `node tests/web-storage-browser.cjs --help` for usage.
The actual UI runs with local source events and no physical panel.
Three languages and five storage modes produce 15 cases per script:
normal, read denied, property denied, write quota exceeded, and both denied.

Each case must load eight tabs, allow local navigation, and preserve the
chosen tab after another Setup event. Forms, backup and page language are
also covered by the regular browser suite.

Storage is optional. The application remembers the selected tab in page
memory when reading or writing the browser preference fails. This does not
store credentials and does not change any device setting.
