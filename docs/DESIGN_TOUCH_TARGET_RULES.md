# Touch-target sizing and map expansion

Public UI acceptance rule for the Waveshare ESP32-S3 Touch LCD 7B.

## Touch targets

- Primary controls should provide a touch target of at least 56 × 56 px.
- Secondary controls should provide a touch target of at least 48 × 48 px.
- Keep at least 8 px between the map expand control and nearby text or the card edge.
- Do not replace the accessible localized map-expand label with an unlocalized "MAX" string.

Original design wording: **второстепенные — от 48×48**.

## Verification

The `tests/lcd-map-expand-static.py` test validates the actual YAML button
width, height, position, spacing and all three localized caption bindings,
then checks that this documented design rule remains present.

This public note intentionally excludes internal workspace paths, machine
identifiers and unrelated historical review materials.
