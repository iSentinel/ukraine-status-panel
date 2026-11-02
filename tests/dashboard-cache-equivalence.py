#!/usr/bin/env python3
"""Prove the native Home map cache is RGB565-equivalent to legacy widgets."""

from __future__ import annotations

import array
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "firmware/assets/dashboard-map-0.115.44"
VIEW = json.loads((ASSETS / "geometry.json").read_text())["views"]["dashboard"]
BACKGROUND = 0x0C1828
OUTLINE = 0x91A8B2
STATES = (0, 1, 2, 16, 4, 32)
PALETTES = {
    0: {0: 0x34424E, 1: 0x1F8A4C, 2: 0xF5BE26, 16: 0xF97316, 4: 0xDC3545, 32: 0x9C27B0},
    1: {0: 0x56616D, 1: 0x0072B2, 2: 0xF0E442, 16: 0xE69F00, 4: 0xD55E00, 32: 0xCC79A7},
    2: {0: 0x4D4D4D, 1: 0xA6A6A6, 2: 0xD9D9D9, 16: 0xBFBFBF, 4: 0xF2F2F2, 32: 0x737373},
}


def rgb(value: int) -> tuple[int, int, int]:
    return (value >> 16 & 0xFF, value >> 8 & 0xFF, value & 0xFF)


def rgb565(value: int) -> int:
    return ((value >> 8) & 0xF800) | ((value >> 5) & 0x07E0) | ((value >> 3) & 0x001F)


def paint(canvas: Image.Image, mask: Image.Image, xy: tuple[int, int], color: int) -> None:
    binary = mask.convert("L").point(lambda pixel: 255 if pixel else 0)
    canvas.paste(Image.new("RGB", binary.size, rgb(color)), xy, binary)


def legacy(states: list[int], palette: int) -> bytes:
    canvas = Image.new("RGB", (VIEW["width"], VIEW["height"]), rgb(BACKGROUND))
    mask_dir = ASSETS / VIEW["mask_dir"]
    for (iso, meta), state in zip(VIEW["regions"].items(), states):
        paint(
            canvas,
            Image.open(mask_dir / f"region-{iso.lower()}.png"),
            (meta["x"], meta["y"]),
            PALETTES[palette][state],
        )
    paint(canvas, Image.open(ASSETS / VIEW["outline"]), (0, 0), OUTLINE)
    values = [rgb565((r << 16) | (g << 8) | b) for r, g, b in canvas.getdata()]
    return array.array("H", values).tobytes()


def cases() -> list[tuple[int, list[int]]]:
    count = len(VIEW["regions"])
    matrix: list[tuple[int, list[int]]] = []
    for palette in PALETTES:
        matrix.extend((palette, [state] * count) for state in STATES)
        matrix.append((palette, [STATES[i % len(STATES)] for i in range(count)]))
        for index in range(count):
            states = [1] * count
            states[index] = 4
            matrix.append((palette, states))
    return matrix


def color_bytes(palette: int, states: list[int]) -> bytes:
    colors = [rgb565(BACKGROUND)]
    colors.extend(rgb565(PALETTES[palette][state]) for state in states)
    colors.append(rgb565(OUTLINE))
    return array.array("H", colors).tobytes()


def build_native(directory: Path) -> Path:
    compiler = shutil.which("g++")
    if compiler is None:
        raise RuntimeError("g++ is required for dashboard cache equivalence")
    binary = directory / "renderer"
    subprocess.run(
        [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-O2",
         "-I", str(ROOT / "firmware/include"), str(ROOT / "tests/dashboard-cache-native.cpp"),
         "-o", str(binary)],
        check=True,
    )
    return binary


def main() -> None:
    if sys.byteorder != "little":
        raise RuntimeError("dashboard cache fixture requires a little-endian host")
    matrix = cases()
    frame_bytes = VIEW["width"] * VIEW["height"] * 2
    with tempfile.TemporaryDirectory(prefix="usp-dashboard-cache-") as temp:
        directory = Path(temp)
        colors = directory / "colors.bin"
        colors.write_bytes(b"".join(color_bytes(*case) for case in matrix))
        result = subprocess.run(
            [str(build_native(directory)), str(colors)],
            check=True,
            capture_output=True,
        )
    if len(result.stdout) != len(matrix) * frame_bytes:
        raise RuntimeError("native dashboard renderer returned incomplete frames")
    for index, (palette, states) in enumerate(matrix):
        actual = result.stdout[index * frame_bytes:(index + 1) * frame_bytes]
        if actual != legacy(states, palette):
            raise RuntimeError(f"dashboard cache pixel mismatch case={index} palette={palette}")
    print(f"DASHBOARD_CACHE_EQUIVALENCE=PASS cases={len(matrix)} pixels_diff=0")


if __name__ == "__main__":
    main()
