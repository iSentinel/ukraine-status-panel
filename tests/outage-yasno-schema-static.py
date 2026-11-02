#!/usr/bin/env python3
"""Guard Yasno Planned/Probable extraction as distinct wire schemas."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CPP = (ROOT / "components" / "usp_outage" / "outage_client.cpp").read_text()
HEADER = (ROOT / "components" / "usp_outage" / "outage_client.h").read_text()
SCANNER = (
    ROOT / "components" / "usp_outage" / "outage_group_scanner.h"
).read_text()


def require(text: str, needle: str, message: str) -> None:
    """Require one exact structural extraction contract."""
    if needle not in text:
        raise AssertionError(message)


def main() -> None:
    """Validate explicit wire schema and bounded exact-path extraction."""
    require(
        HEADER,
        "enum class OutageWireSchema",
        "wire-schema enum missing",
    )
    require(
        CPP,
        "OutageWireSchema::PLANNED",
        "planned fetch does not select planned schema",
    )
    require(
        CPP,
        "OutageWireSchema::PROBABLE",
        "probable fetch does not select probable schema",
    )
    require(
        CPP,
        'const char *planned_path[] = {key};',
        "planned root-group path missing",
    )
    require(
        CPP,
        'const char *probable_path[] = {"25", "dsos", "902", "groups", key};',
        "probable region/DSO/group path missing",
    )
    require(
        SCANNER,
        "static constexpr size_t MAX_PATH = 5;",
        "scanner path bound missing",
    )
    require(
        CPP,
        "static constexpr size_t MAX_OBJECT = 4096;",
        "selected group object bound missing",
    )
    require(
        CPP,
        "!collector.found()",
        "collector found accessor is not called",
    )
    require(
        CPP,
        "collector.overflow()",
        "collector overflow accessor is not called",
    )
    if "BlindGroupObjectScanner" in CPP or "BlindGroupObjectScanner" in SCANNER:
        raise AssertionError("blind group-key scanner returned")
    print("OUTAGE_YASNO_SCHEMA_STATIC=PASS planned probable exact_path bounded")


if __name__ == "__main__":
    main()
