#!/usr/bin/env python3
"""Compile the Spotify playback model with pinned ArduinoJson and sanitizers."""
import json, os, shutil, subprocess, tempfile
from pathlib import Path
from loguru import logger
ROOT=Path(__file__).resolve().parents[1]
VER="7.4.3"
def dep():
    p=Path(os.environ.get("USP_ARDUINOJSON_INCLUDE", ROOT/"firmware/.esphome/build/ukraine-status-panel/managed_components/bblanchon__arduinojson/src"))
    manifest=p.parent/"library.json"
    if not (p/"ArduinoJson.h").is_file() or json.loads(manifest.read_text())["version"] != VER:
        raise RuntimeError("Pinned ArduinoJson 7.4.3 is required")
    return p
def main():
    c=shutil.which("g++")
    if not c: raise RuntimeError("g++ not found")
    with tempfile.TemporaryDirectory(prefix="usp-spotify-playback-") as td:
        b=Path(td)/"playback"
        cmd=[c,"-std=c++17","-Wall","-Wextra","-Werror","-O0","-g","-fsanitize=address,undefined","-fno-omit-frame-pointer","-I",str(ROOT/"components/usp_spotify_auth"),"-isystem",str(dep()),str(ROOT/"tests/spotify-playback-model-native.cpp"),"-o",str(b)]
        subprocess.run(cmd,check=True); subprocess.run([str(b)],check=True)
    logger.info("SPOTIFY_PLAYBACK_MODEL=PASS provider_io=0 device_io=0")
if __name__=="__main__": main()
