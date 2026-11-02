#!/usr/bin/env python3
"""Compile the permanent alarm-audio cadence policy with sanitizers."""
import shutil, subprocess, tempfile
from pathlib import Path
from loguru import logger
ROOT=Path(__file__).resolve().parents[1]
def main():
    with tempfile.TemporaryDirectory(prefix="usp-alarm-audio-") as td:
        binary=Path(td)/"alarm-audio"
        cmd=[shutil.which("g++") or "g++","-std=c++17","-Wall","-Wextra","-Werror","-O0","-g","-fsanitize=address,undefined","-fno-omit-frame-pointer","-I",str(ROOT/"components/usp_alarm"),str(ROOT/"tests/alarm-audio-policy-native.cpp"),"-o",str(binary)]
        subprocess.run(cmd,check=True,cwd=ROOT)
        result=subprocess.run([str(binary)],check=True,capture_output=True,text=True,cwd=ROOT)
        print(result.stdout,end="")
    logger.info("ALARM_AUDIO_POLICY=PASS repeat6000 dismiss disable wrap stress10000")
if __name__=="__main__": main()
