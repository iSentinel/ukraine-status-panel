#!/usr/bin/env python3
"""Verify that the Web installer uses only the pinned local flashing runtime."""
import hashlib
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WEB = ROOT / "web-installer"
VENDOR = WEB / "vendor/esp-web-tools/10.4.0"
EXPECTED_TARBALL = "f18da75335d2f0dca044c4bb052848c0696e7b03cc23d61d8530dd6eba0a9008"
EXPECTED_HEAD = "fab2ba48253d19fd29a15af57bec771d34978b62"

def digest(path: Path) -> str:
    """Hash one vendored runtime file."""
    return hashlib.sha256(path.read_bytes()).hexdigest()

def imported_modules(path: Path) -> list[str]:
    """Collect relative JavaScript imports."""
    text = path.read_text()
    patterns = (
        r'from\s*["\'](\./[^"\']+\.js)["\']',
        r'import\s*\(\s*["\'](\./[^"\']+\.js)["\']\s*\)',
        r'import\s*["\'](\./[^"\']+\.js)["\']',
    )
    found = set()
    for pattern in patterns:
        found.update(re.findall(pattern, text))
    return sorted(found)

def check_closure(runtime: dict[str, str]) -> None:
    """Require all relative imports to resolve locally."""
    root = VENDOR.resolve()
    for name in runtime:
        path = VENDOR / name
        for reference in imported_modules(path):
            target = (path.parent / reference).resolve()
            if root not in target.parents or not target.is_file():
                raise AssertionError(f"unresolved local module {name}: {reference}")

def check_publication_closure() -> None:
    """Require Pages and release archives to ship the pinned local runtime."""
    pages = (ROOT / ".github/workflows/pages.yml").read_text()
    bundle = (ROOT / "scripts/make-public-release.sh").read_text()
    assert "cp -a web-installer/vendor _site/vendor" in pages
    assert 'cp -a "$ROOT/web-installer/vendor" "$bundle/vendor"' in bundle
    hashes = "find vendor -type f -print0 | sort -z | xargs -0 sha256sum"
    assert hashes in pages and hashes in bundle


def main() -> None:
    """Validate provenance, byte hashes, closure and local-only HTML."""
    html = (WEB / "index.html").read_text()
    local = 'src="vendor/esp-web-tools/10.4.0/install-button.js"'
    assert html.count(local) == 1
    assert "unpkg.com" not in html and "esp-web-tools@10" not in html
    provenance = json.loads((VENDOR / "PROVENANCE.json").read_text())
    assert provenance["version"] == "10.4.0"
    assert provenance["source_tarball_sha256"] == EXPECTED_TARBALL
    assert provenance["gitHead"] == EXPECTED_HEAD
    runtime = provenance["runtime_files"]
    assert len(runtime) == 26 and "install-button.js" in runtime
    for name, expected_hash in runtime.items():
        assert digest(VENDOR / name) == expected_hash
    check_closure(runtime)
    check_publication_closure()
    assert (VENDOR / "LICENSE").stat().st_size > 1000
    notices = json.loads((VENDOR / "THIRD_PARTY_LICENSES.json").read_text())
    assert len(notices) >= 10
    print("WEB_INSTALLER_RUNTIME_STATIC=PASS version=10.4.0 files=26 local_only=1 licenses=1")

if __name__ == "__main__":
    main()
