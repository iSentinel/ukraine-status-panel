"""Persist hardware-test commands before sending them and forbid blind replay."""

import inspect
import json
import os
from pathlib import Path
import time

from loguru import logger


class ProbeJournal:
    """Keep original settings and a durable record of every attempted write."""

    def __init__(self, path, initial):
        """Require a fresh file; an older journal must be inspected manually."""
        self.path = Path(path)
        if self.path.exists():
            raise RuntimeError(
                "Existing hardware journal; inspect before continuing"
            )
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.state = {
            "status": "in_progress",
            "initial": initial,
            "commands": {},
        }
        self.save()

    def save(self):
        """Atomically replace the journal after flushing it to storage."""
        temporary = self.path.with_suffix(".tmp")
        self.state["updated_at"] = time.time()
        with temporary.open("w") as stream:
            json.dump(self.state, stream, indent=2)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        temporary.replace(self.path)

    def needs_restore(self, field):
        """Allow a single distinct restore after an attempted test change."""
        commands = self.state["commands"]
        return (
            f"change:{field}" in commands
            and f"restore:{field}" not in commands
        )

    async def once(self, name, desired, send, verify):
        """Save uncertainty before a write; never resend an existing command."""
        commands = self.state["commands"]
        if name in commands:
            raise RuntimeError(
                f"Command {name} already attempted; read its journal"
            )
        operation = {
            "status": "in_doubt",
            "desired": desired,
            "started_at": time.time(),
        }
        commands[name] = operation
        self.save()
        try:
            result = send()
            if inspect.isawaitable(result):
                await result
            operation["status"] = "sent_once"
            self.save()
            result = await verify()
            operation["status"] = "confirmed"
            self.save()
            return result
        except BaseException as error:
            operation["status"] = "in_doubt"
            operation["error"] = type(error).__name__
            self.save()
            raise

    def finish(self, successful):
        """Keep an unresolved restore visible even after the test exits."""
        commands = self.state["commands"]
        fields = sorted(
            name.split(":", 1)[1]
            for name in commands
            if name.startswith("change:")
        )
        unresolved = [
            field
            for field in fields
            if f"change:{field}" in commands
            and commands.get(f"restore:{field}", {}).get("status")
            != "confirmed"
        ]
        self.state["unresolved_settings"] = unresolved
        self.state["status"] = (
            "in_doubt"
            if unresolved
            else "completed" if successful else "completed_with_issues"
        )
        self.save()
        logger.info(
            "Hardware journal status={} unresolved={}",
            self.state["status"],
            unresolved,
        )
