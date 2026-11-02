#!/usr/bin/env python3
"""Run the canonical read-only Spotify Player transport under host SDK shims."""
import json, shutil, subprocess, tempfile
from pathlib import Path
from loguru import logger
ROOT=Path(__file__).resolve().parents[1]
COMP=ROOT/'components/usp_spotify_auth'; SHIMS=ROOT/'tests/spotify_token_host/shims'; VER='7.4.3'
def dep():
    p=ROOT/'firmware/.esphome/build/ukraine-status-panel/managed_components/bblanchon__arduinojson/src'
    if json.loads((p.parent/'library.json').read_text())['version']!=VER: raise RuntimeError('Pinned ArduinoJson 7.4.3 required')
    return p
def run(label,defs=()):
    with tempfile.TemporaryDirectory(prefix='usp-player-transport-') as td:
        binary=Path(td)/label
        cmd=[shutil.which('g++') or 'g++','-std=c++17','-Wall','-Wextra','-Werror','-pthread','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(ROOT/'include'),'-I',str(COMP),'-I',str(SHIMS),'-isystem',str(dep())]
        cmd += ['-D'+d for d in defs]
        cmd += [str(COMP/'playback_transport.cpp'),str(ROOT/'tests/spotify-player-transport-native.cpp'),'-o',str(binary)]
        subprocess.run(cmd,cwd=ROOT,check=True,capture_output=True,text=True)
        r=subprocess.run([str(binary)],cwd=ROOT,check=True,capture_output=True,text=True,timeout=30);print(r.stdout,end='')
def main():
    run('transport',['CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=1']);run('no-cert',['CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=0','HOST_EXPECT_NO_BUNDLE'])
    logger.info('SPOTIFY_PLAYER_TRANSPORT_RUN=PASS provider_io=0 device_io=0')
if __name__=='__main__': main()
