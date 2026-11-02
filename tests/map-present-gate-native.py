#!/usr/bin/env python3
"""Exhaustively compare the production map present gate with legacy behavior."""
from pathlib import Path
import subprocess
import tempfile
from loguru import logger
ROOT=Path(__file__).resolve().parents[1]
CPP=ROOT/'tests/map-present-gate-native.cpp'
HEADER=ROOT/'include/usp_map_present_gate.h'

def build(source,binary,include):
    return subprocess.run(['g++','-std=c++17','-O1','-Wall','-Wextra','-Werror',
        '-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(include),str(source),'-o',str(binary)],
        text=True,capture_output=True)

def main():
    with tempfile.TemporaryDirectory(prefix='usp-present-gate-') as name:
        d=Path(name);base=build(CPP,d/'base',ROOT/'include');assert base.returncode==0,base.stderr
        run=subprocess.run([str(d/'base')],text=True,capture_output=True,check=True);print(run.stdout,end='')
        text=HEADER.read_text();mutants={
          'active':text.replace('if (!pending_ || !active_screen) return false;','if (!pending_) return false;'),
          'rearm':text.replace('    drawing_ = false;','    if (!pending_) drawing_ = false;'),
          'start':text.replace('    if (render_start) drawing_ = true;','    (void) render_start;'),
          'ready':text.replace('    if (!refresh_ready || !drawing_) return false;','    if (!refresh_ready) return false;')}
        caught=0
        for key,value in mutants.items():
            inc=d/key;inc.mkdir();(inc/'usp_map_present_gate.h').write_text(value)
            comp=build(CPP,d/f'mut-{key}',inc)
            if comp.returncode!=0: caught+=1;continue
            proc=subprocess.run([str(d/f'mut-{key}')],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
            assert proc.returncode!=0,key;caught+=1
        assert caught==4
    logger.info('MAP_PRESENT_GATE_NATIVE_RUN=PASS negatives4 ASAN/UBSAN no_device')
if __name__=='__main__':main()
