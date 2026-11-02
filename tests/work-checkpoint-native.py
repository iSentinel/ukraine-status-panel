#!/usr/bin/env python3
"""Exercise work-checkpoint state mirroring without project or device I/O."""

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
from tempfile import TemporaryDirectory

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def prepare(directory):
    """Copy the real shell writer into one disposable project root."""
    root = Path(directory)
    scripts = root / "scripts"
    scripts.mkdir()
    target = scripts / "work-checkpoint.sh"
    shutil.copy2(ROOT / "scripts/work-checkpoint.sh", target)
    (root / ".work-state").mkdir()
    return root, target


def run(script, *args):
    """Invoke the copied writer with the current Python interpreter."""
    env = __import__("os").environ.copy()
    env["PYTHON_BIN"] = sys.executable
    return subprocess.run(
        [str(script), *args],
        env=env,
        capture_output=True,
        text=True,
        check=False,
    )


def current_values(root):
    """Parse the single-line CURRENT representation."""
    result = {}
    for line in (root / ".work-state/CURRENT").read_text().splitlines():
        key, value = line.split("=", 1)
        result[key] = value
    return result


def write_state(root, **changes):
    """Write only the fields used by --from-state."""
    state = dict(
        current_stage="ROADMAP-BOUNDARY-AFTER-402",
        current_task="No source-changing item is unblocked.",
        current_task_status="BLOCKED_EXTERNAL_NO_SOURCE_ITEM",
    )
    state.update(changes)
    path = root / ".work-state/firmware-progress.json"
    path.write_text(json.dumps(state) + "\n")


def from_state_case(root, script):
    """Mirror all three JSON fields and append exactly one action."""
    write_state(root)
    owner_path = root / ".work-state/OWNER.json"
    owner_path.write_text(
        json.dumps(
            {
                "status": "ACTIVE_MAINTENANCE_NO_SOURCE_CHANGE",
                "current_stage": "STALE",
                "note": "stale task",
                "previous_owner": {"current_stage": "HISTORICAL"},
            }
        )
        + "\n"
    )
    result = run(script, "--from-state")
    assert result.returncode == 0, result.stderr
    values = current_values(root)
    assert values["stage"] == "ROADMAP-BOUNDARY-AFTER-402"
    assert values["status"] == "BLOCKED_EXTERNAL_NO_SOURCE_ITEM"
    assert values["message"] == "No source-changing item is unblocked."
    owner = json.loads(owner_path.read_text())
    assert owner["current_stage"] == "ROADMAP-BOUNDARY-AFTER-402"
    assert owner["note"] == "No source-changing item is unblocked."
    assert owner["previous_owner"]["current_stage"] == "HISTORICAL"
    actions = (root / ".work-state/actions.log").read_text().splitlines()
    assert len(actions) == 1 and "ROADMAP-BOUNDARY-AFTER-402" in actions[0]
    return 1


def compatibility_cases(root, script):
    """Keep positional/default-status usage and help compatible."""
    result = run(script, "MANUAL", "manual message", "PASS")
    assert result.returncode == 0
    assert current_values(root)["status"] == "PASS"
    result = run(script, "DEFAULT", "default message")
    assert result.returncode == 0
    assert current_values(root)["status"] == "IN_PROGRESS"
    result = run(script, "--help")
    assert result.returncode == 0 and "--from-state" in result.stdout
    return 3


def refusal_cases(root, script):
    """Reject incomplete or multiline state without replacing CURRENT."""
    before = (root / ".work-state/CURRENT").read_bytes()
    action_before = (root / ".work-state/actions.log").read_bytes()
    write_state(root, current_task_status=None)
    assert run(script, "--from-state").returncode == 2
    assert (root / ".work-state/CURRENT").read_bytes() == before
    write_state(root, current_task="bad\nmessage")
    assert run(script, "--from-state").returncode == 2
    assert (root / ".work-state/CURRENT").read_bytes() == before
    assert (root / ".work-state/actions.log").read_bytes() == action_before
    return 2


def main():
    """Run bounded filesystem-only compatibility and refusal cases."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    with TemporaryDirectory(prefix="usp-work-checkpoint-") as directory:
        root, script = prepare(directory)
        cases = from_state_case(root, script)
        cases += compatibility_cases(root, script)
        cases += refusal_cases(root, script)
    logger.info("WORK_CHECKPOINT_NATIVE=PASS cases={} no_device=1", cases)


if __name__ == "__main__":
    main()
