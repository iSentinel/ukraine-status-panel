#!/usr/bin/env python3
"""Inventory observed firmware dependencies and conservative header references.

The report never deletes files and never labels a header safe to remove.
Use a fresh output directory after a completed production build.
"""

import argparse
from collections import Counter
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import subprocess

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "firmware/.esphome/build/ukraine-status-panel"
TEXT_SUFFIXES = {
    ".h",
    ".hpp",
    ".cpp",
    ".cc",
    ".c",
    ".py",
    ".yaml",
    ".yml",
    ".md",
    ".sh",
    ".txt",
    ".cjs",
    ".js",
    ".json",
    ".in",
}
SKIP_DIRECTORIES = {
    ".git",
    ".esphome",
    "node_modules",
    "__pycache__",
    ".venv",
    "vendor",
}


def digest(path):
    """Hash an observed file without exposing any configuration contents."""
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def arguments():
    """Require explicit completed-image evidence and a fresh report directory."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--release-dir", type=Path, required=True)
    parser.add_argument("--ninja", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    return parser.parse_args()


def verify_release(directory):
    """Refuse a dependency inventory against a different generated main file."""
    manifest = json.loads((directory / "frozen-manifest.json").read_text())
    frozen = directory / "frozen"
    if manifest.get("validation") != "hardware_accepted":
        raise ValueError("Select the completed accepted production release")
    original = frozen / "sources/generated/main.cpp"
    if not original.is_file():
        original = frozen / "sources" / "main.cpp"
    if not original.is_file():
        raise ValueError("The frozen generated main source is missing")
    if digest(original) != digest(BUILD / "src/main.cpp"):
        raise ValueError("Generated main differs from the selected image")
    for name, expected in manifest["checksums"].items():
        if digest(frozen / name) != expected:
            raise ValueError(f"Frozen artifact differs: {name}")
    return manifest, digest(original)


def production_dependencies(ninja, output):
    """Read Ninja's real dependency database; never build a target."""
    command = [str(ninja.resolve()), "-C", str(BUILD / "build"), "-t", "deps"]
    target = output / "ninja-dependencies.txt"
    with target.open("x") as stream:
        subprocess.run(command, stdout=stream, check=True, cwd=ROOT)
    rows = target.read_text().splitlines()
    dependencies = set()
    objects = stale = 0
    for line in rows:
        if not line.startswith("    "):
            if ": #deps " in line:
                objects += 1
                stale += "(STALE)" in line
            continue
        value = Path(line.strip())
        if not value.is_absolute():
            value = BUILD / "build" / value
        dependencies.add(value.resolve())
    if not objects or stale:
        raise ValueError(
            f"Dependency records missing/stale: {objects}/{stale}"
        )
    return dependencies, objects, digest(target)


def text_files():
    """Prune generated trees; include retained artifact helper scripts."""
    names = (
        "include",
        "components",
        "firmware",
        "tests",
        "scripts",
        "tools",
        "docs",
        ".github",
        "web-installer",
        "artifacts",
    )
    files = []
    excluded = SKIP_DIRECTORIES | {"frozen", "sources", "build"}
    artifact_suffixes = {".py", ".sh", ".cpp", ".h", ".yaml", ".yml"}
    for name in names:
        for base, directories, names_here in os.walk(ROOT / name):
            directories[:] = [d for d in directories if d not in excluded]
            for filename in names_here:
                path = Path(base) / filename
                if path.suffix not in TEXT_SUFFIXES:
                    continue
                if (
                    name == "artifacts"
                    and path.suffix not in artifact_suffixes
                ):
                    continue
                if "secret" in path.name.lower() or path.name.startswith(
                    ".env"
                ):
                    continue
                files.append(path)
    files.extend(ROOT.glob("*.md"))
    return sorted(set(files))


def references(headers):
    """Keep textual use evidence, including tests/docs and indirect includes."""
    result = {path: [] for path in headers}
    sizes = Counter()
    for source in text_files():
        content = source.read_text(errors="replace")
        category = source.relative_to(ROOT).parts[0]
        sizes[category] += 1
        for header in headers:
            if source != header and header.name in content:
                result[header].append(str(source.relative_to(ROOT)))
    return result, dict(sizes)


def observed_paths(header, dependencies):
    """Bind copied generated headers to the canonical bytes before counting."""
    generated = BUILD / "src" / header.relative_to(ROOT / "include")
    candidates = {header.resolve(), generated.resolve()}
    observed = sorted(candidates.intersection(dependencies))
    for path in observed:
        if not path.is_file() or digest(path) != digest(header):
            raise ValueError(f"Observed header bytes differ: {header.name}")
    return [str(path) for path in observed]


def header_row(header, dependencies, uses):
    """Separate absent production observations from absence of all references."""
    observed = observed_paths(header, dependencies)
    external = [name for name in uses if not name.startswith("include/")]
    if observed:
        classification = "observed_in_accepted_production_dependencies"
    elif external:
        classification = "not_observed_production_but_other_references_exist"
    elif uses:
        classification = "only_header_references_observed_review_as_a_group"
    else:
        classification = (
            "no_literal_reference_observed_requires_dynamic_review"
        )
    return dict(
        path=str(header.relative_to(ROOT)),
        bytes=header.stat().st_size,
        sha256=digest(header),
        classification=classification,
        observed_dependency_paths=observed,
        reference_count=len(uses),
        external_reference_count=len(external),
        references=uses,
        deletion_authorized=False,
    )


def save_report(output, manifest, main_sha, dependencies, references_data):
    """Preserve full evidence and honest limits instead of a deletion list."""
    paths, objects, dependency_sha = dependencies
    uses, file_counts = references_data
    rows = [header_row(path, paths, uses[path]) for path in sorted(uses)]
    report = dict(
        schema=1,
        observed_at=datetime.now(timezone.utc).isoformat(),
        accepted_version=manifest["version"],
        accepted_source_fingerprint=manifest["source_fingerprint"],
        generated_main_sha256=main_sha,
        ninja_database_objects=objects,
        raw_dependency_report_sha256=dependency_sha,
        scanned_text_files_by_root=file_counts,
        header_count=len(rows),
        header_bytes=sum(row["bytes"] for row in rows),
        classification_counts=dict(
            Counter(row["classification"] for row in rows)
        ),
        rows=rows,
        production_sources_changed=False,
        device_contact=False,
        limits=[
            "ordinary production only; not all profile builds",
            "literal references are conservative, not preprocessor proof",
            "dynamic names/globs and excluded frozen reference trees require review",
            "not observed does not mean safe to delete",
        ],
    )
    with (output / "report.json").open("x") as stream:
        json.dump(report, stream, ensure_ascii=False, indent=2)
        stream.write("\n")
    logger.info("HEADER_USAGE_INVENTORY {}", report["classification_counts"])


def main():
    """Run filesystem-only analysis; no compile, deletion, OTA or network."""
    args = arguments()
    manifest, main_sha = verify_release(args.release_dir.resolve())
    args.output_dir.mkdir(parents=True, exist_ok=False)
    headers = sorted((ROOT / "include").rglob("*.h"))
    dependencies = production_dependencies(args.ninja, args.output_dir)
    save_report(
        args.output_dir, manifest, main_sha, dependencies, references(headers)
    )


if __name__ == "__main__":
    main()
