"""Guard native-size creation before the LCD widget trees exist."""
from pathlib import Path
from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def main():
    """Exclude late resizing and retain normal startup protection."""
    source_path = ROOT / "components/lvgl/lvgl_esphome.cpp"
    source = source_path.read_text()
    start = source.index("LvglComponent::LvglComponent(")
    end = source.index("void LvglComponent::set_resolution_", start)
    constructor = source[start:end]
    assert "lv_display_create(240, 240)" not in constructor
    native_create = (
        "lv_display_create(this->displays_[0]->get_native_width(),"
    )
    assert native_create in constructor
    assert "this->displays_[0]->get_native_height()" in constructor
    for phase in ("start", "configured", "ready"):
        assert "LVGL_SETUP_PROFILE phase=" + phase in source
    logger.info("LVGL_NATIVE_RESOLUTION_STATIC=PASS no_placeholder_resize")


if __name__ == "__main__":
    main()
