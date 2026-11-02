#!/usr/bin/env python3
"""Guard the ESPHome 2026.10 LVGL host/debug split for the large panel UI."""

import os
from pathlib import Path
import shutil

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
LVGL = ROOT / "components/lvgl"
SHARED = (ROOT / "firmware/ukraine-status-panel.yaml").read_text()
PRIVATE = (ROOT / "firmware/ukraine-status-panel-production.yaml").read_text()
DEVICE_CPP = (LVGL / "lvgl_esphome.cpp").read_text()
HOST_CPP = (LVGL / "lvgl_esphome_host.cpp").read_text()
DEFINES = (LVGL / "defines.py").read_text()
COMPONENT = (LVGL / "__init__.py").read_text()
WIDGETS = (LVGL / "widgets/__init__.py").read_text()
LIST_WIDGETS = (LVGL / "widgets/lv_list.py").read_text()


def installed_upstream() -> Path | None:
    """Locate the LVGL implementation belonging to the pinned ESPHome binary."""
    binary = os.environ.get("ESPHOME_BIN") or shutil.which("esphome")
    if not binary:
        return None
    prefix = Path(binary).resolve().parent.parent
    matches = list(
        prefix.glob("lib/python*/site-packages/esphome/components/lvgl/lvgl_esphome.cpp")
    )
    return matches[0] if matches else None


def main() -> None:
    """Keep debug outlines CI/host-only while preserving the ESP32 fast path."""
    assert 'lvgl_debug_outline: "false"' in SHARED
    assert "debug_outline: ${lvgl_debug_outline}" in SHARED
    assert 'lvgl_debug_outline: "false"' in PRIVATE
    assert not list((ROOT / "firmware").glob("*debug-outline*.yaml")), (
        "a full-device debug_outline profile is unsafe for this large generated setup()"
    )

    assert 'CONF_DEBUG_OUTLINE = "debug_outline"' in DEFINES
    assert "next_debug_outline_color" in DEFINES
    assert "cv.Optional(df.CONF_DEBUG_OUTLINE, default=False)" in COMPONENT
    assert "apply_debug_outline(w)" in WIDGETS
    assert "apply_debug_outline(w)" in LIST_WIDGETS

    assert DEVICE_CPP.startswith("#ifndef USE_HOST\n")
    assert DEVICE_CPP.rstrip().endswith("#endif  // !USE_HOST")
    assert HOST_CPP.startswith("#ifdef USE_HOST\n")
    assert HOST_CPP.rstrip().endswith("#endif  // USE_HOST")
    assert 'esphome/components/mipi_rgb/mipi_rgb.h' in DEVICE_CPP
    assert 'esphome/components/mipi_rgb/mipi_rgb.h' not in HOST_CPP

    upstream = installed_upstream()
    if upstream is not None:
        body = HOST_CPP.removeprefix(
            "#ifdef USE_HOST\n// Generic host implementation synced from ESPHome 2026.10.0.\n"
        ).removesuffix("\n#endif  // USE_HOST\n")
        assert body == upstream.read_text(), "host shim drifted from pinned ESPHome LVGL"

    logger.info(
        "LVGL_2026_10_HOST_DEBUG_STATIC=PASS default_off device_fast_path host_upstream dynamic_widgets"
    )


if __name__ == "__main__":
    main()
