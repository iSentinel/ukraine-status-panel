# Known issues — published snapshot 0.115.407-dev

The public source checkout is **0.115.407-dev** and the first-flash
release metadata remains `validation-required`. It is not a public
hardware-validated firmware image.

A separately maintained physical panel has installed `0.115.415-dev`.
The last accepted rollback is `0.115.414-dev`. That private source,
compiled image, hardware evidence and production configuration are not
present in this public checkout. A public `.407` CI pass does not
qualify the installed `.415` image.

## Pending validations

- Verify the Wi-Fi indicator on the physical display.
- Perform public USB first-flash acceptance before approving an installer.
- Shelly is read-only; a disconnected meter must display `--` rather
  than old voltage. An offline meter does not prove a mains outage.
- Test optional SHT4x and Li-Po modules on real hardware.
- Test real Spotify login persistence, playback commands and album artwork.
- Qualify additional Shelly models separately in a future firmware version.

The `.415` Owner Approval process is manual, not an independent
Ed25519 signature. Private logs, credentials and device addresses are
intentionally omitted.
