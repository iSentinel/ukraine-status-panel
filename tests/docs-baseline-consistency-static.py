#!/usr/bin/env python3
"""Keep current-baseline documentation aligned with firmware metadata."""

import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FIRMWARE = ROOT / "firmware" / "ukraine-status-panel.yaml"


def firmware_version() -> str:
    """Read the canonical firmware version without parsing ESPHome YAML."""
    text = FIRMWARE.read_text()
    match = re.search(r"^  firmware_version: \"([^\"]+)\"$", text,
                      re.MULTILINE)
    if match is None:
        raise AssertionError("canonical firmware_version not found")
    return match.group(1)


def require_version(path: str, version: str) -> None:
    """Require the current version in a current-baseline document."""
    text = (ROOT / path).read_text()
    assert version in text, f"{path} does not mention current {version}"


def current_head_checks(version: str) -> None:
    """Reject stale baseline claims in current roadmap/resume sections."""
    roadmap = (ROOT / "ROADMAP.md").read_text()
    roadmap_head = roadmap.split(
        "# Historical accepted 0.115.401-dev", 1
    )[0]
    assert version in roadmap_head, "ROADMAP head is not on current firmware"
    stale_roadmap = "current accepted and installed physical baseline"
    assert stale_roadmap not in roadmap_head

    resume = (ROOT / "RESUME.md").read_text()
    resume_head = resume.split(
        "# Historical resume — accepted 0.115.401-dev", 1
    )[0]
    assert version in resume_head, "RESUME head is not on current firmware"
    stale_401 = "Current accepted/installed physical baseline is `0.115.401-dev`"
    assert stale_401 not in resume_head
    stale_resume = (
        ".240 пока не собрана" in resume_head
        or "устройство .239" in resume_head
    )
    assert not stale_resume, "RESUME head contradicts accepted .240 state"


def release_status_checks(version: str) -> str:
    """Validate fail-closed public release metadata and return status."""
    path = ROOT / "web-installer" / "release-status.json"
    status = json.loads(path.read_text())
    assert status["version"] == version
    assert status["status"] in {"validation-required", "hardware-validated"}
    if status["status"] == "validation-required":
        assert status["public_first_flash_hardware_validated"] is False
    return status["status"]


def main() -> None:
    """Validate baseline docs and fail-closed public release metadata."""
    version = firmware_version()
    require_version("docs/KNOWN_ISSUES.md", version)
    require_version("docs/WEB_UI_ARCHITECTURE.md", version)
    require_version("ROADMAP.md", version)

    qa_text = (ROOT / "docs" / "QA.md").read_text()
    stale_flash = re.search(
        r"flash(?:ing)?[^\n]{0,80}0\.115\."
        r"(?:14[0-9]|1[5-9][0-9]|2[0-3][0-9])-dev",
        qa_text,
        re.IGNORECASE,
    )
    assert stale_flash is None, "QA instructs flashing an historical firmware"
    assert "current validated firmware baseline" in qa_text
    current_head_checks(version)
    release = release_status_checks(version)
    print(f"DOCS_BASELINE_CONSISTENCY=PASS version={version} release={release}")


if __name__ == "__main__":
    main()
