#!/usr/bin/env python3
"""Check readable weather timestamps against the generated panel font."""

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
PROGRAM = r"""
#include "usp_weather_time.h"
#include <cstdio>
int main() {
  const char *values[] = {"", "2026-10-23T07:15", "2026-10-23T23:59:59",
      "9999-12-31T23:59", "8888-08-08T08:08", "invalid"};
  unsigned index = 0;
  for (const char *value : values) {
    for (unsigned language = 0; language < 3; ++language) {
      const auto text = usp_weather_time::as_of(value, language);
      std::printf("%u\t%u\t%s\n", index, language, text.c_str());
    }
    ++index;
  }
}
"""


def metrics_module():
    """Reuse the real glyph reader rather than duplicating font parsing."""
    path = ROOT / "tests/weather-label-width-generated.py"
    spec = importlib.util.spec_from_file_location("weather_metrics", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def measurements(raw, source):
    """Require every caption to fit the existing weather time label."""
    metrics = metrics_module()
    samples = []
    for name in ("weather_updated_label", "fullscreen_weather_updated_label"):
        size, limit = metrics.widget(source, name)
        values = metrics.glyphs(raw.decode(), size)
        for case, language, text in metrics.labels(PROGRAM):
            width = metrics.text_width(text, values)
            assert width <= limit, (name, case, language, text, width, limit)
            samples.append(
                {
                    "widget": name,
                    "case": int(case),
                    "language": int(language),
                    "width": width,
                    "limit": limit,
                    "size": size,
                }
            )
    return samples


def main():
    """Save source-bound evidence without a network request or panel action."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--main-cpp", type=Path, required=True)
    parser.add_argument(
        "--output",
        type=Path,
        required=True,
        help="New JSON path; existing evidence is refused",
    )
    args = parser.parse_args()
    assert not args.output.exists(), "Evidence already exists"
    raw = args.main_cpp.read_bytes()
    source = (ROOT / "firmware/ukraine-status-panel.yaml").read_text()
    samples = measurements(raw, source)
    result = {
        "status": "PASS",
        "measurements": len(samples),
        "generated_main_sha256": hashlib.sha256(raw).hexdigest(),
        "time_header_sha256": hashlib.sha256(
            (ROOT / "include/usp_weather_time.h").read_bytes()
        ).hexdigest(),
        "single_line_fit": True,
        "visual_hardware_acceptance": False,
        "samples": samples,
    }
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    logger.info("WEATHER_TIME_WIDTH=PASS measurements={}", len(samples))


if __name__ == "__main__":
    main()
