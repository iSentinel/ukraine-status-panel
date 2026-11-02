#!/usr/bin/env python3
"""Non-destructive live probe for the 0.115.143 optional-hardware UI."""

from __future__ import annotations

import asyncio
import os
from pathlib import Path

from history_native_api_common import build_client, verify_device

ROOT = Path(__file__).resolve().parents[1]
CONFIG = Path(os.environ.get("USP_CONFIG", ROOT / "firmware/ukraine-status-panel.yaml"))
ADDRESS = os.environ.get("USP_ADDRESS", "ukraine-status-panel.local")
TEST_BUZZER = os.environ.get("USP_TEST_BUZZER", "0") == "1"
REQUIRED_SERVICES = {
    "usp_qa_show_diagnostics",
    "usp_qa_set_sound_143",
    "usp_qa_buzzer_test_143",
    "usp_qa_dump_optional_hardware_143",
}
REQUIRED_ENTITIES = {
    "SHT4x Status",
    "Environment Temperature",
    "Environment Humidity",
    "Panel Sounds Enabled",
    "Buzzer Test",
}


async def main() -> None:
    client = build_client(CONFIG, ADDRESS)
    await client.connect(login=True)
    try:
        await verify_device(client, CONFIG)
        entities, services = await client.list_entities_services()
        names = {str(getattr(item, "name", "")) for item in entities}
        service_map = {str(item.name): item for item in services}
        missing_entities = sorted(REQUIRED_ENTITIES - names)
        missing_services = sorted(REQUIRED_SERVICES - set(service_map))
        if missing_entities:
            raise RuntimeError(f"Missing optional-hardware entities: {missing_entities}")
        if missing_services:
            raise RuntimeError(f"Missing optional-hardware services: {missing_services}")

        await client.execute_service(service_map["usp_qa_show_diagnostics"], {})
        await client.execute_service(service_map["usp_qa_dump_optional_hardware_143"], {})
        print("OPTIONAL_HARDWARE_SURFACE=PASS")
        if TEST_BUZZER:
            print("OPTIONAL_HARDWARE_BUZZER_TEST=REQUESTED")
            await client.execute_service(service_map["usp_qa_buzzer_test_143"], {})
        else:
            print("OPTIONAL_HARDWARE_BUZZER_TEST=SKIPPED set USP_TEST_BUZZER=1 to enable")
        print("OPTIONAL_HARDWARE_LIVE=PASS")
    finally:
        await client.disconnect()


if __name__ == "__main__":
    asyncio.run(main())
