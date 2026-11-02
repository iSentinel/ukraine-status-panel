#!/usr/bin/env python3
"""Guard Home mirror text delivery without reading text back from LVGL widgets."""
import os
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CONFIGS = (
    ROOT / "firmware/ukraine-status-panel.yaml",
    ROOT / "firmware/ukraine-status-panel-public-first-flash.yaml",
    ROOT / "firmware/ukraine-status-panel-production.yaml",
)


def selected_configs() -> tuple[Path, ...]:
    """Return an isolated override or all production projections."""
    override = os.environ.get("USP_CONFIG")
    return (Path(override),) if override else DEFAULT_CONFIGS


def block(text: str, start: str, end: str) -> str:
    """Return one exact script block from the selected firmware config."""
    begin = text.index(start)
    finish = text.index(end, begin)
    return text[begin:finish]


def check_pair(source: str, primary: str, mirror: str) -> None:
    """Require one formatter evaluation and ordered delivery to both labels."""
    assert f"lv_label_get_text(id({primary}))" not in source
    assert source.count("const auto build_text = [&]() -> std::string {") == 1
    assert source.count("const std::string value = build_text();") == 1
    steps = (
        f"lv_label_set_text(id({primary}), value.c_str());",
        f"lv_obj_send_event(id({primary}), lvgl::lv_update_event, nullptr);",
        f"lv_label_set_text(id({mirror}), value.c_str());",
        f"lv_obj_send_event(id({mirror}), lvgl::lv_update_event, nullptr);",
    )
    positions = []
    for step in steps:
        assert source.count(step) == 1, step
        positions.append(source.index(step))
    assert positions == sorted(positions)


def verify(config: Path) -> None:
    """Verify one canonical or generated firmware profile."""
    text = config.read_text()
    threat = block(
        text,
        "  - id: refresh_selected_threat_ui\n",
        "  - id: refresh_selected_telemetry_ui\n",
    )
    grid = block(
        text,
        "  - id: refresh_selected_telemetry_ui\n",
        "  - id: refresh_indoor_environment_ui\n",
    )
    assert threat.count("- delay: 1ms") == 1
    assert grid.count("- delay: 1ms") == 1
    check_pair(threat, "selected_threat_value", "home_threat_status_label")
    check_pair(grid, "selected_grid_value", "home_grid_status_label")
    for token in (
        "if (!id(selected_threat_valid))",
        "if (!id(selected_threat_supported))",
        "id(selected_threat_state) == 1",
        "usp_alert_rows::kind(id(selected_threat_state), id(ui_language))",
    ):
        assert token in threat, token
    for token in (
        "if (!id(selected_energy_valid))",
        "if (!id(selected_energy_known))",
        "status == 3",
        "status == 4",
        "status == 9",
        "status == 10",
    ):
        assert token in grid, token
    print(
        "HOME_TEXT_DELIVERY_STATIC=PASS "
        f"config={config.name} pairs=2 formatter_evaluations=2 "
        "widget_reads=0 events=4"
    )


for config in selected_configs():
    verify(config)
