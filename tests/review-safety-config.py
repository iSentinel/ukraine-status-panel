#!/usr/bin/env python3
"""Validate public network isolation and the LCD safety configuration."""
import argparse
import importlib.util
from pathlib import Path
from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def module(name, relative):
    """Load a project checker without resolving credentials or contacting devices."""
    spec = importlib.util.spec_from_file_location(name, ROOT / relative)
    loaded = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(loaded)
    return loaded


def public_policy():
    """Exercise the real generator, including rejected network-control mutations."""
    generator = module(
        "public_generator", "scripts/generate-public-first-flash.py"
    )
    source = ROOT / "firmware/ukraine-status-panel.yaml"
    generated = generator.generate(source)
    public = ROOT / "firmware/ukraine-status-panel-public-first-flash.yaml"
    assert (
        generated == public.read_text()
    ), "Public profile was not regenerated"
    for injection in ["\napi:\n", "\nota:\n", "\nx: !secret leaked\n"]:
        try:
            generator.verify_public_profile(generated + injection)
        except RuntimeError:
            continue
        raise AssertionError(
            "Public profile accepted a private/network mutation"
        )
    assert "MANIFEST CHECK ONLY" in source.read_text()
    assert "SIGNED MANIFEST / DOWNGRADE DENY" not in source.read_text()


def widgets(items):
    """Visit only actual child widgets, leaving automation dictionaries alone."""
    for item in items:
        kind, value = next(iter(item.items()))
        yield kind, value
        yield from widgets(value.get("widgets", []))


def lcd_policy(config):
    """Keep readable flat controls and reject actions that begin on finger down."""
    lvgl = config["lvgl"][0]
    theme = lvgl["theme"]
    assert theme["dark_mode"] is True
    assert theme["obj"]["pad_all"] == 0
    assert theme["label"]["text_color"] == 0xF8FAFC
    assert theme["button"]["shadow_width"] == 0
    all_widgets = list(widgets(lvgl["top_layer"]["widgets"]))
    for page in lvgl["pages"]:
        all_widgets.extend(widgets(page.get("widgets", [])))
    by_id = {value.get("id"): value for _, value in all_widgets}
    for kind, value in all_widgets:
        if kind == "button":
            assert "on_press" not in value, value.get("id")
    restart = by_id["panel_restart_ui_button"]
    assert restart["on_click"] == [{"script.execute": "confirm_panel_restart"}]
    for slot in range(3):
        assert (
            by_id[f"alarm_enabled_{slot}"]["checked"]["bg_color"] == 0x166534
        )


def main():
    """Run deterministic checks without a server or physical panel connection."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    public_policy()
    config = module(
        "lcd_reference", "tests/lcd-navigation-static.py"
    ).load_config()
    lcd_policy(config)
    logger.info(
        "REVIEW_SAFETY_CONFIG=PASS public_isolation=1 negative=3 lcd_click=1"
    )


if __name__ == "__main__":
    main()
