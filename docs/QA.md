# Public QA and release status

The public firmware source is **0.115.407-dev** and is not a
hardware-validated first-flash release. Its installer metadata must
remain `validation-required` until separate physical acceptance.

The phrase **current validated firmware baseline** refers here to
the separately managed panel's last accepted rollback,
`0.115.414-dev`. The physical panel is running `0.115.415-dev`,
whose final release acceptance is still pending manual Owner Approval.
Those newer image and evidence files are not in this public checkout.

Run the static/native/LVGL/browser/configuration tests before any new
build. A clean CI result validates the files in the checked-out commit,
not a separately installed image. New production versions require a
guarded build, one authorized OTA attempt, UART/runtime evidence and
a human visual acceptance when UI changes.

Do not flash older firmware just to satisfy tests; preserve the
accepted rollback. Shelly is read-only: offline or stale readings
must show unavailable voltage, and offline does not establish whether
mains electricity is off.

Owner Approval is an explicit owner declaration, not an independent
Ed25519 signature. It never automatically flashes firmware.
