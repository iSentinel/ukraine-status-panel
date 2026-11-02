#!/usr/bin/env python3
"""Execute publication-workflow and dependency-lock policy checks."""
from __future__ import annotations

import argparse
from copy import deepcopy
from pathlib import Path
import re

from loguru import logger
import yaml

ROOT = Path(__file__).resolve().parents[1]
FILES = {
    "source": ".github/workflows/source-check.yml",
    "release": ".github/workflows/release.yml",
    "pages": ".github/workflows/pages.yml",
    "lock": "requirements-ci.lock",
    "bootstrap": "requirements-ci-bootstrap.lock",
    "installer": "scripts/install-ci-dependencies.sh",
    "bundle": "scripts/make-public-release.sh",
    "dev": "requirements-dev.txt",
}


def locked(text: str) -> dict[str, tuple[str, str]]:
    """Require one exact version and SHA256 for every Python package."""
    result = {}
    pattern = (
        r"([a-z0-9_.-]+)==([A-Za-z0-9_.!+~-]+) --hash=sha256:([0-9a-f]{64})"
    )
    for line in text.splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        match = re.fullmatch(pattern, line)
        assert match, f"Unpinned or unhashed dependency: {line}"
        name = match[1].lower().replace("_", "-")
        assert name not in result, f"Duplicate package: {name}"
        result[name] = match[2], match[3]
    assert result, "Empty dependency lock"
    return result


def workflows(data: dict) -> None:
    """Reject mutable external actions or any unlocked installation path."""
    for key in ("source", "release", "pages"):
        value = yaml.safe_load(data[key])
        for job in value["jobs"].values():
            if "uses" in job:
                assert job["uses"] == "./.github/workflows/source-check.yml"
                continue
            assert job["runs-on"] == "ubuntu-24.04"
            runs = [step.get("run", "") for step in job["steps"]]
            assert any(
                "bash scripts/install-ci-dependencies.sh" in r for r in runs
            )
            assert not any("pip install" in r for r in runs)
            for step in job["steps"]:
                if "uses" in step:
                    assert re.fullmatch(
                        r"actions/[\w-]+@[0-9a-f]{40}", step["uses"]
                    )
        if key != "source":
            publication = "release" if key == "release" else "build-and-deploy"
            assert value["jobs"][publication]["needs"] == "validation"


def gates(data: dict) -> None:
    """Require image checks on the staged bytes before upload or archiving."""
    for key in ("pages", "release"):
        value = yaml.safe_load(data[key])
        name = "release" if key == "release" else "build-and-deploy"
        steps = value["jobs"][name]["steps"]
        checked = [
            (i, step["run"])
            for i, step in enumerate(steps)
            if "--require-hardware-validated" in step.get("run", "")
        ]
        assert len(checked) == 1
        index, command = checked[0]
        assert "--factory-image" in command
        assert "--acceptance-preflight" in data[key]
        if key == "pages":
            assert "--site-dir _site" in command
            assert 'cp "$factory" _site/firmware.factory.bin' in command
            upload = next(
                i
                for i, s in enumerate(steps)
                if s.get("uses", "").startswith("actions/upload-pages")
            )
            assert index < upload
        else:
            assert command.index("--factory-image") < command.index(
                "scripts/make-public-release.sh"
            )
    bundle = data["bundle"]
    assert bundle.index('cp "$FACTORY"') < bundle.index("--factory-image")
    assert bundle.index("--factory-image") < bundle.index("tar --sort")
    assert '--site-dir "$bundle"' in bundle


def candidate_policy(data: dict) -> None:
    """Allow candidate builds only on an explicit manual request."""
    source = yaml.safe_load(data["source"])
    triggers = source.get("on", source.get(True, {}))
    request = triggers["workflow_dispatch"]["inputs"]["build_public_candidate"]
    assert request["type"] == "boolean" and request["default"] is False
    steps = source["jobs"]["source-check"]["steps"]
    condition = (
        "github.event_name == 'workflow_dispatch' && "
        "inputs.build_public_candidate"
    )
    selected = [
        step
        for step in steps
        if "esphome compile " in step.get("run", "")
        or step.get("uses", "").startswith("actions/upload-artifact@")
    ]
    assert len(selected) == 2
    expected_condition = "$" + "{{ " + condition + " }}"
    assert all(step.get("if") == expected_condition for step in selected)
    assert "record-public-candidate.py" in selected[0]["run"]
    publication_artifacts(data)


def publication_artifacts(data: dict) -> None:
    """Download the selected same-repository artifact instead of rebuilding."""
    for key, job_name in (
        ("release", "release"),
        ("pages", "build-and-deploy"),
    ):
        publication = yaml.safe_load(data[key])["jobs"][job_name]["steps"]
        assert not any(
            "esphome compile " in step.get("run", "") for step in publication
        )
        downloads = [
            step
            for step in publication
            if step.get("uses", "").startswith("actions/download-artifact@")
        ]
        assert len(downloads) == 1
        assert downloads[0]["with"]["path"] == "_accepted"
        assert downloads[0]["with"]["name"] == (
            "$" + "{{ steps.accepted_factory.outputs.artifact_name }}"
        )
        assert downloads[0]["with"]["run-id"] == (
            "$" + "{{ steps.accepted_factory.outputs.run_id }}"
        )


def validate(data: dict) -> None:
    """Validate actual workflows, exact locks and source-build tooling."""
    workflows(data)
    candidate_policy(data)
    gates(data)
    main, bootstrap = locked(data["lock"]), locked(data["bootstrap"])
    assert main["esphome"][0] == "2026.10.1", "CI ESPHome differs from production pin"
    assert {"pip", "setuptools", "wheel", "packaging"} <= bootstrap.keys()
    assert all(main[name] == version for name, version in bootstrap.items())
    for line in data["dev"].splitlines():
        if line.strip() and not line.startswith("#"):
            name, version = line.split("==")
            assert main[name.lower().replace("_", "-")][0] == version
    installer = data["installer"]
    assert installer.count("--require-hashes") == 2
    assert "--no-build-isolation" in installer
    assert "requirements-ci-bootstrap.lock" in installer
    assert "requirements-ci.lock" in installer
    assert "python -m pip check" in installer


TEXT_MUTATIONS = (
    ("installer", "--require-hashes", ""),
    ("lock", "esphome==2026.10.1", "esphome==2026.9.1"),
    ("lock", "--hash=sha256:", "--hash=sha1:"),
    ("pages", "--factory-image", "--unverified-image"),
    ("release", "needs: validation", "needs: []"),
    (
        "source",
        "github.event_name == 'workflow_dispatch' && "
        "inputs.build_public_candidate",
        "true",
    ),
    (
        "release",
        'factory="_accepted/firmware.factory.bin"',
        "esphome compile "
        "firmware/ukraine-status-panel-public-first-flash.yaml",
    ),
)


def mutation_cases(data: dict) -> list[dict]:
    """Create unsafe workflow and dependency inputs without editing files."""
    cases = []
    changed = deepcopy(data)
    changed["source"] = re.sub(
        r"actions/checkout@[0-9a-f]{40}",
        "actions/checkout@v4",
        changed["source"],
    )
    cases.append(changed)
    for key, before, after in TEXT_MUTATIONS:
        changed = deepcopy(data)
        assert before in changed[key]
        changed[key] = changed[key].replace(before, after)
        cases.append(changed)
    changed = deepcopy(data)
    changed["bootstrap"] = re.sub(
        r"sha256:[0-9a-f]{64}",
        "sha256:" + "0" * 64,
        changed["bootstrap"],
        count=1,
    )
    cases.append(changed)
    return cases


def reject_mutations(data: dict) -> int:
    """Require rejection of every unsafe policy input."""
    cases = mutation_cases(data)
    for changed in cases:
        try:
            validate(changed)
        except (AssertionError, KeyError, ValueError, StopIteration):
            continue
        raise AssertionError("Unsafe CI policy mutation was accepted")
    return len(cases)


def main() -> None:
    """Inspect files only; do not dispatch workflows, build, or publish."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    data = {name: (ROOT / path).read_text() for name, path in FILES.items()}
    validate(data)
    count = reject_mutations(data)
    logger.info(
        "CI_RELEASE_INTEGRITY_NATIVE=PASS negative={} packages={}",
        count,
        len(locked(data["lock"])),
    )


if __name__ == "__main__":
    main()
