# Public development resume — 0.115.407-dev

The published source and public first-flash profile are still at
`0.115.407-dev`; installer release status is `validation-required`.
This public checkout excludes private deployment records and binaries.

Separately, the physical device is installed with `0.115.415-dev`,
while `0.115.414-dev` is the last accepted rollback. Manual Owner
Approval and the separate checkpoint transaction remain outstanding;
public CI does not approve the private installed image.

Next steps: complete public source CI, fix portable test dependencies,
review Owner Approval before merging, and test the public USB first
flash on physical hardware. Future Spotify/Shelly work is staged
outside this public snapshot. No production build or OTA is triggered
from this resume document.
