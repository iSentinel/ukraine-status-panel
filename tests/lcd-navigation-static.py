#!/usr/bin/env python3
"""Check shared LCD navigation reachability, labels and touch geometry."""

from pathlib import Path

from loguru import logger
import yaml

ROOT = Path(__file__).resolve().parents[1]


class ReferenceLoader(yaml.SafeLoader):
    """Read configuration values without resolving private YAML tags."""


def unknown_tag(loader, tag, node):
    """Keep tagged values as data without accessing external files."""
    if isinstance(node, yaml.ScalarNode):
        return loader.construct_scalar(node)
    if isinstance(node, yaml.SequenceNode):
        return loader.construct_sequence(node)
    return loader.construct_mapping(node)


def load_config():
    """Read the production document without loading secret values."""
    ReferenceLoader.add_multi_constructor("!", unknown_tag)
    path = ROOT / "firmware/ukraine-status-panel.yaml"
    return yaml.load(path.read_text(), Loader=ReferenceLoader)


def check_bar(lvgl):
    """Require eight labelled destinations with non-overlapping touch targets."""
    bar = lvgl["top_layer"]["widgets"][0]["obj"]
    assert bar["id"] == "lcd_navigation_bar"
    buttons = [item["button"] for item in bar["widgets"]]
    assert len(buttons) == 8
    expected = ("home", "map", "weather", "power", "history", "spotify", "clock", "settings")
    pages = (0, 1, 2, 3, 4, 12, 9, 5)
    last_edge = -8
    for index, (name, button) in enumerate(zip(expected, buttons)):
        assert button["id"] == f"lcd_nav_{name}"
        assert button["text"]
        assert button["width"] >= 48 and button["height"] >= 48
        assert button["x"] - last_edge >= 8
        last_edge = button["x"] + button["width"]
        action = button["on_click"][0]["script.execute"]
        assert action == {"id": "navigate_lcd_page", "page": pages[index]}
    assert last_edge <= bar["width"]
    return bar


def check_pages(lvgl, bar):
    """Check all ordinary pages use the same bar and leave room for it."""
    pages = {page["id"]: page for page in lvgl["pages"]}
    names = ("home", "map", "weather", "power", "history", "settings", "clock", "spotify")
    page_ids = (0, 1, 2, 3, 4, 5, 9, 12)
    for index, name in enumerate(names):
        page = pages[f"page_{name}"]
        matching = [
            item.get("script.execute")
            for item in page["on_load"]
            if isinstance(item.get("script.execute"), dict)
            and item["script.execute"].get("id") == "sync_lcd_navigation_page"
        ]
        assert matching == [{"id": "sync_lcd_navigation_page", "page": page_ids[index]}]
        for widget in page["widgets"]:
            kind, obj = next(iter(widget.items()))
            if obj.get("hidden") or "y" not in obj:
                continue
            if kind == "button":
                assert obj["y"] < bar["y"], obj.get("id")
            if kind == "obj" and "height" in obj:
                assert obj["y"] + obj["height"] <= bar["y"], name
    footer = next(
        item["label"]
        for item in pages["page_weather"]["widgets"]
        if "label" in item
        and item["label"].get("id") == "weather_footer_attribution_label"
    )
    assert footer["y"] < bar["y"]


def check_controller_singleton():
    """Keep the custom controller independent of ESPHome global include order."""
    header = (ROOT / "include/usp_lcd_navigation_controller.h").read_text()
    source = (ROOT / "firmware/ukraine-status-panel.yaml").read_text()
    assert "inline Controller &controller()" in header
    assert "static Controller instance;" in header
    assert "type: usp_lcd_navigation::Controller" not in source
    assert "id(lcd_navigation_controller)" not in source
    for call in (
        "usp_lcd_navigation::controller().attach(",
        "usp_lcd_navigation::controller().take_action()",
        "usp_lcd_navigation::controller().qa_touch(",
        "usp_lcd_navigation::controller().set_page(",
        "usp_lcd_navigation::controller().set_fullscreen(",
    ):
        assert call in source, call


def main():
    """Validate navigation plus preservation of the accepted TLS configuration."""
    check_controller_singleton()
    config = load_config()
    lvgl = config["lvgl"][0]
    bar = check_bar(lvgl)
    check_pages(lvgl, bar)
    scripts = {script["id"]: script for script in config["script"]}
    route = scripts["navigate_lcd_page"]["then"]
    assert route[0]["if"]["then"] == [{"script.execute": "show_home_fast"}]
    for index, name in enumerate(
        ("map", "weather", "power", "history", "settings"), 1
    ):
        actions = route[index]["if"]["then"]
        last = actions[-1]
        if name in ("map", "weather"):
            branch = last["if"]
            assert (
                branch["then"][0]["lvgl.page.show"]["id"]
                == f"page_{name}_full"
            )
            assert branch["else"][0]["lvgl.page.show"]["id"] == f"page_{name}"
        else:
            assert last["lvgl.page.show"]["id"] == f"page_{name}"
    refresh = scripts["refresh_lcd_navigation"]["then"][0]["lambda"]
    assert "usp_navigation_text::page_label(language, index)" in refresh
    source = (ROOT / "firmware/ukraine-status-panel.yaml").read_text()
    assert "include/usp_navigation_text.h" in source
    catalog = (ROOT / "include/usp_navigation_text.h").read_text()
    for word in ("Погода", "Weather", "Огляд", "Обзор", "Overview"):
        assert word in catalog
    tls = config["esp32"]["framework"]["sdkconfig_options"]
    assert tls["CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC"] == "y"
    assert tls["CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC"] == "n"
    logger.info(
        "LCD_NAVIGATION_STATIC=PASS routes=8 visible_weather=1 visible_clock=1 visible_spotify=1 geometry=1"
    )


if __name__ == "__main__":
    main()
