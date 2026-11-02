#!/usr/bin/env python3
"""Vendor the exact esp-web-tools 10.4.0 browser runtime without rebuilding it."""
from __future__ import annotations
import argparse
import hashlib
import json
import shutil
import tarfile
from pathlib import Path
from loguru import logger

VERSION = "10.4.0"
TARBALL_SHA256 = "f18da75335d2f0dca044c4bb052848c0696e7b03cc23d61d8530dd6eba0a9008"
NPM_INTEGRITY = (
    "sha512-3pwkeFFm5Fj7UQo8SJNYK5RXrtNCpq6X9QoI6bMT4GBZWgrJqjn0"
    "YvM9ihG74BtMoSFYXfmDtkehuxe50PTMPQ=="
)
GIT_HEAD = "fab2ba48253d19fd29a15af57bec771d34978b62"
RUNTIME_PREFIX = "package/dist/web/"

def sha256_bytes(data: bytes) -> str:
    """Return SHA256 hex."""
    return hashlib.sha256(data).hexdigest()

def sha256_file(path: Path) -> str:
    """Hash a file in chunks."""
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()

def parse_args() -> argparse.Namespace:
    """Parse local-only inputs."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("tarball", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--review-dir", type=Path, required=True)
    return parser.parse_args()

def read_member(archive: tarfile.TarFile, name: str) -> bytes:
    """Read one regular file."""
    member = archive.getmember(name)
    if not member.isfile():
        raise ValueError(f"not a file: {name}")
    handle = archive.extractfile(member)
    if handle is None:
        raise ValueError(f"cannot read: {name}")
    return handle.read()

def runtime_members(archive: tarfile.TarFile) -> list[tarfile.TarInfo]:
    """Return the published browser runtime."""
    members = [m for m in archive.getmembers()
               if m.isfile() and m.name.startswith(RUNTIME_PREFIX)]
    if len(members) != 26:
        raise ValueError(f"expected 26 runtime files, found {len(members)}")
    return sorted(members, key=lambda item: item.name)

def write_runtime(archive: tarfile.TarFile, dest: Path) -> dict[str, str]:
    """Write runtime bytes and collect hashes."""
    hashes = {}
    for member in runtime_members(archive):
        relative = member.name.removeprefix(RUNTIME_PREFIX)
        data = read_member(archive, member.name)
        target = dest / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        hashes[relative] = sha256_bytes(data)
    return hashes

def copy_licenses(review: Path, dest: Path, archive: tarfile.TarFile) -> None:
    """Copy upstream and locked dependency license texts."""
    license_dir = dest / "licenses"
    license_dir.mkdir(parents=True, exist_ok=True)
    (dest / "LICENSE").write_bytes(read_member(archive, "package/LICENSE"))
    entries = json.loads((review / "dependency-licenses.json").read_text())
    locked = review / "locked-dependencies"
    for entry in entries:
        name = entry["name"].replace("@", "").replace("/", "-")
        if entry["files"]:
            source = locked / entry["path"] / entry["files"][0]
        else:
            source = review / "ssr-dom-shim-LICENSE"
        shutil.copyfile(source, license_dir / f"{name}-{entry['version']}.txt")
    shutil.copyfile(review / "dependency-licenses.json",
                    dest / "THIRD_PARTY_LICENSES.json")

def write_metadata(dest: Path, hashes: dict[str, str], package: dict) -> None:
    """Record immutable source metadata and runtime hashes."""
    metadata = {
        "name": "esp-web-tools", "version": VERSION,
        "license": package["license"], "gitHead": GIT_HEAD,
        "npm_integrity": NPM_INTEGRITY,
        "source_tarball_sha256": TARBALL_SHA256,
        "runtime_files": hashes,
    }
    (dest / "PROVENANCE.json").write_text(
        json.dumps(metadata, indent=2) + "\n")

def main() -> None:
    """Verify the package then replace the local vendor tree."""
    args = parse_args()
    actual = sha256_file(args.tarball)
    if actual != TARBALL_SHA256:
        raise SystemExit(f"tarball SHA256 mismatch: {actual}")
    if args.destination.exists():
        shutil.rmtree(args.destination)
    args.destination.mkdir(parents=True)
    with tarfile.open(args.tarball, "r:gz") as archive:
        package = json.loads(read_member(archive, "package/package.json"))
        if package.get("version") != VERSION:
            raise SystemExit(f"package version mismatch: {package.get('version')}")
        hashes = write_runtime(archive, args.destination)
        copy_licenses(args.review_dir, args.destination, archive)
    write_metadata(args.destination, hashes, package)
    logger.info("VENDORED_ESP_WEB_TOOLS=PASS version={} runtime_files={}",
                VERSION, len(hashes))

if __name__ == "__main__":
    main()
