# Public Installer — Page Override

**Mode:** Operate / guided provisioning

The installer has one job: get a blank Waveshare panel from USB connection to a trustworthy Wi-Fi provisioning state without implying that an unvalidated image is stable.

- Keep the product dark-blue identity and opaque surfaces.
- Show three ordered steps: USB, install, provision Wi-Fi.
- Use neutral language tabs with one restrained active cue.
- Keep all language controls at least 44px with visible keyboard focus.
- Keep the page title at app scale (maximum 36px), not launch-page hero scale.
- State erase/provisioning consequences near the install action.
- Do not publish merely because CI can compile a factory image; `release-status.json` is the exact-version hardware acceptance gate.
- Test 320/375/768/1024 and UA/RU/EN with zero horizontal overflow.
- No glass, gradients, glow, parallax, scroll reveal or decorative motion.
