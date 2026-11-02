#!/usr/bin/env python3
"""Build the pure Spotify playback presentation contract with ASAN/UBSAN."""
from pathlib import Path
import json, os, shutil, subprocess, tempfile
from loguru import logger
ROOT=Path(__file__).resolve().parents[1]
VER='7.4.3'
def dep():
    p=Path(os.environ.get('USP_ARDUINOJSON_INCLUDE', ROOT/'firmware/.esphome/build/ukraine-status-panel/managed_components/bblanchon__arduinojson/src'))
    if not (p/'ArduinoJson.h').is_file() or json.loads((p.parent/'library.json').read_text())['version'] != VER:
        raise RuntimeError('Pinned ArduinoJson 7.4.3 is required')
    return p
def main():
    c=shutil.which('g++') or 'g++'
    with tempfile.TemporaryDirectory(prefix='usp-spotify-presentation-') as td:
        b=Path(td)/'presentation'
        cmd=[c,'-std=c++17','-Wall','-Wextra','-Werror','-O0','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(ROOT/'include'),'-I',str(ROOT/'components/usp_spotify_auth'),'-isystem',str(dep()),str(ROOT/'tests/spotify-playback-presentation-native.cpp'),'-o',str(b)]
        subprocess.run(cmd,cwd=ROOT,check=True,capture_output=True,text=True)
        r=subprocess.run([str(b)],cwd=ROOT,check=True,capture_output=True,text=True,timeout=30)
        print(r.stdout,end='')
    logger.info('SPOTIFY_PLAYBACK_PRESENTATION_RUN=PASS provider_io=0 artwork_io=0 device_io=0')
if __name__=='__main__': main()
