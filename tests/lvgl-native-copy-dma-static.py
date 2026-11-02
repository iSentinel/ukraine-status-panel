#!/usr/bin/env python3
"""Guard C-D4 native framebuffer DMA routing without touching global LVGL IO."""

from pathlib import Path

from loguru import logger


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "components/lvgl/lvgl_esphome.cpp").read_text()
POLICY = (ROOT / "components/lvgl/lvgl_native_copy_policy.h").read_text()


def need(condition: bool, message: str) -> None:
    """Fail with a focused contract message."""
    if not condition:
        raise AssertionError(message)


def main() -> None:
    """Verify bounded native-only routing and synchronous DMA completion."""
    need("NATIVE_COPY_DMA_THRESHOLD = 64U * 1024U" in SOURCE,
         "DMA threshold must remain 64 KiB")
    need("display->buf_1->handlers = &native_copy_dma.handlers" in SOURCE,
         "buf_1 custom handler missing")
    need("display->buf_2->handlers = &native_copy_dma.handlers" in SOURCE,
         "buf_2 custom handler missing")
    need("native_copy_pair(dest, src)" in SOURCE,
         "handler must be limited to the native pair")
    need("src->header.stride != dest->header.stride" in SOURCE,
         "stride mismatch must fail back to LVGL")
    need("xSemaphoreTake(native_copy_dma.done, portMAX_DELAY)" in SOURCE,
         "submitted DMA must not race a CPU fallback")
    need("native_copy_dma_bytes(\n      buf3" in SOURCE,
         "Home resident capture must share the DMA helper")
    need("area.x1 != 0" in POLICY and "stride != row_bytes" in POLICY,
         "policy must remain full-width and contiguous")
    logger.info(
        "LVGL_NATIVE_COPY_DMA_STATIC=PASS threshold64k native_pair "
        "sync_wait home_shared fallback_safe"
    )


if __name__ == "__main__":
    main()
