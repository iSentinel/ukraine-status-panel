#!/usr/bin/env python3
"""Exercise real journal handling for failures after a delivered device write."""

import asyncio
import json
from pathlib import Path
import tempfile

from hardware_probe_journal import ProbeJournal


async def test_failures(path):
    """Check original recovery data, uncertainty and one distinct restoration."""
    journal = ProbeJournal(path, {"group": 10, "brightness": 100})
    calls = []

    def delivered():
        assert (
            json.loads(path.read_text())["commands"]["change:group"]["status"]
            == "in_doubt"
        )
        calls.append("change")

    async def lost_reply():
        raise TimeoutError(
            "Simulated lost reply after the command reached the device"
        )

    try:
        await journal.once("change:group", 11, delivered, lost_reply)
    except TimeoutError:
        pass
    assert journal.needs_restore("group")
    assert journal.state["initial"]["group"] == 10

    async def restored():
        return 10

    await journal.once(
        "restore:group", 10, lambda: calls.append("restore"), restored
    )
    assert not journal.needs_restore("group")
    try:
        await journal.once(
            "restore:group", 10, lambda: calls.append("duplicate"), restored
        )
    except RuntimeError:
        pass
    assert calls == ["change", "restore"]
    journal.finish(False)
    assert journal.state["status"] == "completed_with_issues"


async def test_uncertain_restore(path):
    """An uncertain restoration is recorded and cannot be sent twice."""
    journal = ProbeJournal(path, {"group": 10})

    async def success():
        return 11

    async def timeout():
        raise TimeoutError("Lost restoration response")

    await journal.once("change:group", 11, lambda: None, success)
    try:
        await journal.once("restore:group", 10, lambda: None, timeout)
    except TimeoutError:
        pass
    assert not journal.needs_restore("group")
    journal.finish(False)
    assert journal.state["status"] == "in_doubt"
    assert journal.state["unresolved_settings"] == ["group"]


async def test_navigation_mode(path):
    """New settings must stay unresolved until their restore is confirmed."""
    journal = ProbeJournal(path, {"mode": 0})

    async def success():
        return True

    await journal.once("change:mode", 1, lambda: None, success)
    journal.finish(False)
    assert journal.state["unresolved_settings"] == ["mode"]
    await journal.once("restore:mode", 0, lambda: None, success)
    journal.finish(True)
    assert journal.state["status"] == "completed"
    assert journal.state["unresolved_settings"] == []


async def main():
    """Use isolated temporary journals and no hardware connections."""
    with tempfile.TemporaryDirectory(prefix="usp-journal-") as directory:
        await test_failures(Path(directory) / "restored.json")
        await test_uncertain_restore(Path(directory) / "uncertain.json")
        await test_navigation_mode(Path(directory) / "mode.json")
    print(
        "HARDWARE_PROBE_JOURNAL=PASS durable_intent=1 restore_once=1 no_replay=1"
    )


if __name__ == "__main__":
    asyncio.run(main())
