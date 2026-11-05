# Building public firmware source (0.115.407-dev)

The published source snapshot is `0.115.407-dev`; it is not a
hardware-validated release and must not be mistaken for the separately
installed physical firmware `0.115.415-dev`. The last accepted physical
rollback is `0.115.414-dev`.

Use the repository production wrapper, not an unguarded raw build:

```bash
./scripts/compile-production.sh --help
./scripts/compile-production.sh --fingerprint
```

The fingerprint command is read-only. For a future explicitly authorized
production build, consult the wrapper help and run the wrapper against
a dedicated candidate with an unconsumed build allowance.

**Never use the published .407 snapshot to overwrite the .415 device**.
The original .415 Build1/OTA1 allowance is exhausted; do not attempt a
second build or OTA of that version. Public first-flash is
`validation-required` until real hardware acceptance has been performed.

Use static/native/browser/configuration checks before any new build.
Do not include local passwords, keys, hardware evidence or generated
binary artifacts in GitHub.
