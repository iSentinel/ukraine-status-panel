#!/usr/bin/env python3
"""Execute the real button generator's creation method with recorded LVGL calls."""
import argparse
import ast
import asyncio
from pathlib import Path
from types import SimpleNamespace
from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


class Label:
    """Represent only the alignment contract handled by this generator."""

    def __init__(self):
        self.centered = False


class Parent:
    """Keep created children separate for each test case."""

    def __init__(self):
        self.children = []


class LvCalls:
    """Record effects of the emitted creation and alignment operations."""

    def label_create(self, parent):
        parent.children.append(Label())

    def obj_center(self, child):
        child.centered = True


def creation_method(source):
    """Compile only the actual method, without importing or changing ESPHome state."""
    tree = ast.parse(source.read_text())
    node = next(
        n
        for n in ast.walk(tree)
        if isinstance(n, ast.AsyncFunctionDef) and n.name == "on_create"
    )
    module = ast.fix_missing_locations(
        ast.Module(body=[node], type_ignores=[])
    )
    namespace = {
        "CONF_TEXT": "text",
        "MockObj": object,
        "lv": LvCalls(),
        "lv_expr": SimpleNamespace(
            obj_get_child=lambda parent, index: parent.children[index]
        ),
    }
    exec(compile(module, str(source), "exec"), namespace)
    return namespace["on_create"]


async def check(method):
    """Require centered automatic text, including empty text, and preserve custom UI."""
    for value in ["Save", ""]:
        parent = Parent()
        assert await method(None, parent, {"text": value}) is parent
        assert len(parent.children) == 1
        assert parent.children[
            0
        ].centered, "Automatic button text is not centered"
    parent = Parent()
    await method(None, parent, {"widgets": []})
    assert not parent.children, "Custom button children must not be replaced"


def main():
    """Check the same generator method used by the production configuration."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--source",
        type=Path,
        default=ROOT / "components/lvgl/widgets/button.py",
    )
    args = parser.parse_args()
    asyncio.run(check(creation_method(args.source)))
    logger.info(
        "BUTTON_GENERATOR=PASS actual_method automatic_text empty_text custom_widgets"
    )


if __name__ == "__main__":
    main()
