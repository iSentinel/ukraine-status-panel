#!/usr/bin/env python3
"""Exercise the real LVGL DIRECT sync through the custom large-copy route."""

from pathlib import Path
import os
import subprocess

from loguru import logger


ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / "artifacts" / "0.115.259-cd4-frame-sync" / "after-host"
DEFAULT_LVGL = ROOT / "firmware/.esphome/build/ukraine-status-panel"
DEFAULT_LVGL /= "managed_components/lvgl__lvgl"
LVGL = Path(os.environ.get("USP_LVGL_ROOT", DEFAULT_LVGL))
CMAKE = Path("/root/.cache/esphome/idf/tools/cmake/3.30.2/bin/cmake")
NINJA = Path("/root/.cache/esphome/idf/tools/ninja/1.12.1/ninja")


def write_cmake() -> None:
    """Write a sanitizer-backed host project against the real LVGL tree."""
    WORK.mkdir(parents=True, exist_ok=True)
    cmake = f"""cmake_minimum_required(VERSION 3.16)
project(cd4_after LANGUAGES C CXX)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL \"\" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL \"\" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL \"\" FORCE)
set(LV_BUILD_CONF_PATH \"{ROOT / 'tests/lvgl_navigation_host/lv_conf.h'}\"
    CACHE PATH \"\" FORCE)
add_subdirectory(\"{LVGL}\" lvgl)
add_executable(cd4_after \"{ROOT / 'tests/lvgl-direct-sync-copy-native.cpp'}\")
target_compile_features(cd4_after PRIVATE cxx_std_17)
target_include_directories(cd4_after PRIVATE
    \"{LVGL / 'src'}\" \"{ROOT / 'components/lvgl'}\")
target_compile_options(cd4_after PRIVATE -Wall -Wextra -Werror
    -fsanitize=address,undefined -fno-omit-frame-pointer)
target_link_options(cd4_after PRIVATE -fsanitize=address,undefined)
target_link_libraries(cd4_after PRIVATE lvgl m)
"""
    (WORK / "CMakeLists.txt").write_text(cmake)


def main() -> None:
    """Configure, compile and execute the actual LVGL sync route."""
    write_cmake()
    build = WORK / "build"
    subprocess.run(
        [
            str(CMAKE),
            "-S",
            str(WORK),
            "-B",
            str(build),
            "-G",
            "Ninja",
            f"-DCMAKE_MAKE_PROGRAM={NINJA}",
        ],
        check=True,
        stdout=subprocess.DEVNULL,
    )
    subprocess.run(
        [str(CMAKE), "--build", str(build), "-j2"],
        check=True,
        stdout=subprocess.DEVNULL,
    )
    result = subprocess.run(
        [str(build / "cd4_after")], capture_output=True, text=True, check=True
    )
    logger.info("{}", result.stdout.strip())


if __name__ == "__main__":
    main()
