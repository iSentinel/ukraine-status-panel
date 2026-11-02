"""Require a cooperative cold-map worker and hidden unfinished images."""

import argparse
from pathlib import Path

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def script(source, name):
    """Read one actual YAML script without including its later neighbours."""
    start = source.index(f"  - id: {name}\n")
    end = source.find("\n  - id:", start + 1)
    return source[start : end if end >= 0 else len(source)]


def main():
    """Bind the proven renderer to bounded yielded production navigation."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    source = (ROOT / "firmware/ukraine-status-panel.yaml").read_text()
    worker = script(source, "prepare_fullscreen_map_cache")
    assert "mode: single" in worker
    step = "Cache::instance().cold_step(4096U);"
    assert worker.count(step) == 1
    assert worker.count("delay: 1ms") == 2
    assert worker.index("delay: 1ms") < worker.index("- while:")
    assert worker.index(step) < worker.rindex("delay: 1ms")
    assert "Cache::instance().cold_pending()" in worker
    assert source.count("id(prepare_fullscreen_map_cache).execute();") == 1
    header = (ROOT / "include/usp_fullscreen_map_cache.h").read_text()
    assert "image_ != nullptr && renderer_.ready()" in header
    assert header.index("lv_obj_add_flag(image_, LV_OBJ_FLAG_HIDDEN)") < header.index(
        "lv_image_set_src(image_"
    )
    assert "std::copy_n(originals, count, originals_);" in header
    assert "if (count > ORIGINAL_CAPACITY" in header
    assert "FULLSCREEN_CACHE_COLD_BEGIN" in header
    assert "FULLSCREEN_CACHE_COLD_READY" in header
    assert "max_slice_ms" in header and "setup_ms" in header
    logger.info(
        "FULLSCREEN_COLD_STATIC=PASS worker1ms/4096px owned references hidden partial frame"
    )


if __name__ == "__main__":
    main()
