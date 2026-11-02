#!/usr/bin/env python3
"""Check the fullscreen setting, its local escape and mode-aware routes."""

import importlib.util
from pathlib import Path

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def config_data():
    """Reuse the reference-only YAML reader from the menu geometry check."""
    path = ROOT / "tests/lcd-navigation-static.py"
    spec = importlib.util.spec_from_file_location("lcd_nav_static", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.load_config()


def check_setting(config):
    """Require one saved value and one shared path for LCD, Web and API."""
    globals_by_id = {value["id"]: value for value in config["globals"]}
    mode = globals_by_id["lcd_navigation_mode_value"]
    assert mode["restore_value"] is True and mode["initial_value"] == "0"
    assert globals_by_id["lcd_navigation_help_seen"]["restore_value"] is True
    select = next(
        item
        for item in config["select"]
        if item.get("id") == "lcd_navigation_mode_select"
    )
    assert select["options"] == ["Buttons", "Fullscreen"]
    assert (
        select["set_action"][0]["script.execute"]["id"]
        == "set_lcd_navigation_mode"
    )
    assert select["web_server"]["sorting_group_id"] == "web_group_settings"
    source = (ROOT / "firmware/ukraine-status-panel.yaml").read_text()
    assert source.count("id(lcd_navigation_mode_value) = ") == 1
    scripts = {item["id"]: item for item in config["script"]}
    backend = scripts["set_lcd_navigation_mode"]["then"][0]["lambda"]
    assert "invalidate_home_native_frame_cache" in backend
    assert "publish_state" in backend


def find_objects(config):
    """Return top-level overlay objects by their stable IDs."""
    result = {}
    for widget in config["lvgl"][0]["top_layer"]["widgets"]:
        _, value = next(iter(widget.items()))
        result[value["id"]] = value
    return result


def check_escape(config):
    """Require a bounded local header area and visible button recovery."""
    objects = find_objects(config)
    header = objects["lcd_navigation_escape_zone"]
    assert header["width"] == 300 and header["height"] == 64
    assert header["clickable"] is True and header["scrollable"] is False
    menu = objects["lcd_navigation_menu"]
    assert menu["hidden"] is True and menu["clickable"] is True
    children = {
        next(iter(item.values()))["id"]: next(iter(item.values()))
        for item in menu["widgets"]
    }
    buttons = children["lcd_navigation_menu_buttons"]["on_click"]
    action = buttons[-1]["script.execute"]
    assert action == {"id": "set_lcd_navigation_mode", "fullscreen": False}
    for key in (
        "lcd_navigation_menu_buttons",
        "lcd_navigation_menu_close",
        "lcd_navigation_menu_settings",
    ):
        assert children[key]["height"] >= 48
    controller = (ROOT / "include/usp_lcd_navigation_controller.h").read_text()
    for word in (
        "lv_indev_stop_processing",
        "LV_EVENT_SHORT_CLICKED",
        "LV_EVENT_RELEASED",
        "LV_EVENT_CLICKED",
        "LV_EVENT_PRESSING",
    ):
        assert word in controller
    assert "HTTP" not in controller and "network::" not in controller


def check_routes(config):
    """Keep Settings outside the main cycle but allow fullscreen swipe escape."""
    scripts = {item["id"]: item for item in config["script"]}
    routes = scripts["navigate_lcd_page"]["then"]
    for index, name in ((1, "map"), (2, "weather")):
        branch = routes[index]["if"]["then"][-1]["if"]
        assert "fullscreen_mode" in branch["condition"]["lambda"]
        assert branch["then"][0]["lvgl.page.show"]["id"] == f"page_{name}_full"
        assert branch["else"][0]["lvgl.page.show"]["id"] == f"page_{name}"
    controller = (ROOT / "include/usp_lcd_navigation_controller.h").read_text()
    assert "this->page_ == 8 || target == nullptr" in controller
    assert "this->page_ == 5 ||" not in controller
    swipe = (ROOT / "include/usp_swipe_navigation.h").read_text()
    assert "if (page == 5)" in swipe
    assert "if (page == 10)" in swipe
    assert swipe.count("Action::NEXT) return 0") >= 2
    assert swipe.count("Action::PREVIOUS) return 9") >= 2
    assert "MAIN_PAGES[] = {0, 1, 2, 3, 4, 12, 9}" in swipe
    interval = next(
        value for value in config["interval"] if value["interval"] == "16ms"
    )
    dispatch = interval["then"][0]["lambda"]
    assert "take_action" in dispatch and "adjacent_page" in dispatch
    assert "qa_touch" not in dispatch
    assert "lcd_navigation_ready" in dispatch


def main():
    """Validate integration; the native test covers gesture behavior."""
    config = config_data()
    check_setting(config)
    check_escape(config)
    check_routes(config)
    source = (ROOT / "firmware/webui/usp-web-ui.src.js").read_text()
    assert '["LCD Mode","Навігація LCD","Навигация LCD"]' in source
    logger.info(
        "LCD_FULLSCREEN_STATIC=PASS shared_mode local_escape routes budget"
    )


if __name__ == "__main__":
    main()
