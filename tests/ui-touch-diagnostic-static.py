#!/usr/bin/env python3
"""Guard the production diagnostics touch self-test against prototype UI text."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
YAML = (ROOT / "firmware/ukraine-status-panel.yaml").read_text()
HEADER = (ROOT / "include/usp_diagnostics_text.h").read_text()


def main() -> None:
    """Keep touch semantics while allowing captions to come from the shared catalog."""
    assert "id: lv_touch_test" in YAML
    assert 'text: "PRESS ME"' not in YAML
    assert 'text: "TOUCH: PRESSED"' not in YAML
    for text in ("ТЕСТ ДОТИКУ", "ТЕСТ КАСАНИЯ", "TOUCH TEST"):
        assert text in HEADER
    for text in ("ДОТИК: OK", "КАСАНИЕ: OK", "TOUCH: OK"):
        assert text in HEADER

    start = YAML.index("              id: lv_touch_test")
    end = YAML.index(
        "          - label:\n              id: backlight_value_label",
        start,
    )
    block = YAML[start:end]
    ok_call = "usp_diagnostics_text::Label::TOUCH_OK"
    reset_call = "usp_diagnostics_text::Label::TOUCH_TEST"
    assert block.count(ok_call) == 3
    assert block.count(reset_call) == 3
    assert block.count('text: "TOUCH TEST"') == 1
    assert "width: 420" in block and "height: 56" in block
    assert block.count("bg_color: 0x168A50") == 1
    assert block.count("bg_color: 0x16263A") >= 1
    assert "border_width: 1" in block and "border_color: 0x263D57" in block
    print(
        "UI_TOUCH_DIAGNOSTIC_STATIC=PASS "
        "touch420x56 catalog_ok3 reset3 semantic_pressed_ok palette=surface"
    )


if __name__ == "__main__":
    main()
