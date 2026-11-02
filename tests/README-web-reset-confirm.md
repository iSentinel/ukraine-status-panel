# Reset confirmation in the browser

Run `node tests/web-reset-confirm-browser.cjs --help` for usage.
Select the readable or embedded JavaScript with --script.
An optional --output saves a new JSON report.

Chromium uses local source events and HTTP fixtures only. It checks Reset
Network, Reset Integrations and Restart in English, Russian and Ukrainian:
cancel sends no request; acceptance permits one pending request; a source
update cannot bypass the pending guard; an HTTP error allows a later retry.
No physical panel, network profile, credential or integration is reset.

The production UI uses the existing localized action label in its question.
Other button actions keep their existing meaning. The pending guard belongs
to the UI and does not replace backend checks or authentication.
