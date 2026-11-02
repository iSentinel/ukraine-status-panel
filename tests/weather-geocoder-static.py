#!/usr/bin/env python3
"""Require explicit weather city searches to capture the current menu language."""

import argparse
import re
from pathlib import Path

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def arguments(source, start):
    """Read one C++ call while retaining nested id() and string operations."""
    depth = 1
    index = start
    while depth:
        if source[index] == "(":
            depth += 1
        elif source[index] == ")":
            depth -= 1
        index += 1
    return source[start:index - 1]


def main():
    """Check real LCD/Web bindings without starting a search or using the panel."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    source = (ROOT / "firmware/ukraine-status-panel.yaml").read_text()
    pattern = r"id\(weather_client\)\.request_city_search\("
    calls = list(re.finditer(pattern, source))
    assert calls, "No explicit city search bindings found"
    for call in calls:
        value = arguments(source, call.end())
        assert re.search(r",\s*id\(ui_language\)\s*$", value), value
    logger.info("WEATHER_GEOCODER_BINDINGS=PASS calls={} current_UI_locale", len(calls))


if __name__ == "__main__":
    main()
