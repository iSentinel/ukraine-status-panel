#!/usr/bin/env python3
"""Check that publishing depends on the complete offline regression workflow."""
from copy import deepcopy
from pathlib import Path
import argparse
import yaml
from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def validate(source, publications):
    """Reject skipped regression, unpinned LVGL, or publishing without validation."""
    trigger = source.get("on", source.get(True, {}))
    assert "workflow_call" in trigger, "Source validation is not reusable"
    steps = source["jobs"]["source-check"]["steps"]
    regression = [
        step
        for step in steps
        if "bash scripts/run-regression.sh" in step.get("run", "")
    ]
    assert len(regression) == 1, "Complete regression is missing or duplicated"
    assert regression[0].get("env", {}).get("USP_RUN_LIVE") == "0"
    assert "USP_LVGL_ROOT" in regression[0]["run"], "Real LVGL test omitted"
    runner = (ROOT / "scripts/run-regression.sh").read_text()
    browser = runner[runner.index('if [[ "${USP_RUN_BROWSER:-0}" == "1" ]]') :]
    assert 'PLAYWRIGHT_BROWSERS_PATH' in browser
    assert '$ROOT/../.qa-tools/playwright' in browser
    assert browser.index('export PLAYWRIGHT_BROWSERS_PATH') < browser.index(
        'spotify-auth-return-browser.cjs'
    )
    assert "85aa60d18b3d5e5588d7b247abf90198f07c8a63" in str(steps)
    for workflow, job_name in publications:
        jobs = workflow["jobs"]
        assert (
            jobs["validation"]["uses"]
            == "./.github/workflows/source-check.yml"
        )
        assert (
            jobs[job_name]["needs"] == "validation"
        ), "Publishing bypasses tests"


def reject_mutations(source, publications):
    """Prove that removing a dependency or enabling live QA fails validation."""
    changed = deepcopy(publications)
    changed[0][0]["jobs"][changed[0][1]]["needs"] = []
    changed_source = deepcopy(source)
    for step in changed_source["jobs"]["source-check"]["steps"]:
        if "bash scripts/run-regression.sh" in step.get("run", ""):
            step["env"]["USP_RUN_LIVE"] = "1"
    for candidate, outputs in [
        (source, changed),
        (changed_source, publications),
    ]:
        try:
            validate(candidate, outputs)
        except AssertionError:
            continue
        raise AssertionError("A broken publishing workflow passed validation")


def main():
    """Read workflow definitions and exercise valid and invalid configurations."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    directory = ROOT / ".github/workflows"
    source = yaml.safe_load((directory / "source-check.yml").read_text())
    publications = [
        (yaml.safe_load((directory / name).read_text()), job)
        for name, job in [
            ("release.yml", "release"),
            ("pages.yml", "build-and-deploy"),
        ]
    ]
    validate(source, publications)
    reject_mutations(source, publications)
    logger.info(
        "CI_REGRESSION_DEPENDENCY=PASS positive=1 negative=2 live_device=0"
    )


if __name__ == "__main__":
    main()
