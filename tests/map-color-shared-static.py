#!/usr/bin/env python3
"""Keep all map color consumers bound to the accepted shared calculation."""
import argparse
from pathlib import Path
import re

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
PROFILES = (
    "ukraine-status-panel.yaml",
    "ukraine-status-panel-public-first-flash.yaml",
    "ukraine-status-panel-production.yaml",
)
CALL = re.compile(
    r"(?m)^([ ]*)uint32_t color = usp_alert_region_colors::color_hex\(\n"
    r"\1    state, palette\);\n"
)


def verify(path):
    """Require both actual YAML calls with their original state and palette."""
    source = path.read_text()
    assert len(list(CALL.finditer(source))) == 2, path.name
    assert (
        source.count("uint32_t color = usp_alert_region_colors::color_hex(")
        == 2
    )
    assert "include/usp_fullscreen_map_cache.h" in source


def main():
    """Check canonical, public and ordinary private profiles without secrets."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, action="append")
    args = parser.parse_args()
    paths = args.config or [ROOT / "firmware" / name for name in PROFILES]
    for path in paths:
        verify(path)
    header = (ROOT / "include/usp_fullscreen_map_cache.h").read_text()
    start = header.index("inline uint32_t region_color_hex(")
    end = header.index("\n}\n", start) + 3
    body = header[start:end]
    assert (
        body.count(
            "return usp_alert_region_colors::color_hex(state, palette);"
        )
        == 1
    )
    assert "0x34424E" not in body
    assert '#include "usp_alert_region_colors.h"' in header
    logger.info(
        "MAP_COLOR_SHARED_STATIC=PASS profiles={} yaml_calls=2 header_wrapper=1",
        len(paths),
    )


if __name__ == "__main__":
    main()
