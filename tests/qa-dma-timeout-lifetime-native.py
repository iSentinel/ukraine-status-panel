#!/usr/bin/env python3
"""Verify accepted QA DMA requests cannot outlive stack callback state."""
from __future__ import annotations

import hashlib
import os
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "components/lvgl/lvgl_esphome.cpp"
FIXTURE = ROOT / "tests/qa-dma-timeout-lifetime-native.cpp.in"


def extract_method(source: str) -> str:
    """Extract the exact production QA method with balanced braces."""
    marker = "bool LvglComponent::qa_present_cached_native_frame_dma()"
    start = source.index(marker)
    opened = source.index("{", start)
    end, depth = opened + 1, 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def build_harness(source: str) -> str:
    """Embed the real production callback context and QA method."""
    start = source.index("struct QaFrameDmaWaitContext")
    end = source.index("\n#endif", start)
    real = source[start:end] + "\n" + extract_method(source)
    return FIXTURE.read_text().replace("// PRODUCTION_CONTEXT_AND_METHOD", real)


def compile_fixture(work: Path, source: str) -> None:
    """Compile the exact production method under ASan/UBSan."""
    cpp = work / "test.cpp"
    cpp.write_text(build_harness(source))
    command = [
        "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-g", "-O0",
        "-fno-inline", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-fno-pie", "-no-pie", str(cpp), "-o", str(work / "test"),
    ]
    subprocess.run(command, check=True, capture_output=True, text=True, timeout=60)


def execute(work: Path, case: str) -> subprocess.CompletedProcess[str]:
    """Execute one DMA completion timing case with after-return detection."""
    env = dict(os.environ)
    env["ASAN_OPTIONS"] = "detect_stack_use_after_return=1:detect_leaks=0"
    return subprocess.run(
        [str(work / "test"), case], capture_output=True, text=True,
        timeout=20, env=env,
    )


def main() -> None:
    """Require normal, rejected-submission and delayed-completion safety."""
    before = SOURCE.read_bytes()
    source = before.decode()
    assert "xSemaphoreTake(done, portMAX_DELAY);" in extract_method(source)
    mutant = source.replace(
        "    xSemaphoreTake(done, portMAX_DELAY);\n", "", 1
    )
    assert mutant != source
    with tempfile.TemporaryDirectory(prefix="usp-qa-dma-") as tmp:
        work = Path(tmp)
        compile_fixture(work, source)
        cases = ("normal", "reject", "late")
        results = {case: execute(work, case) for case in cases}
        mutant_work = work / "mutant"
        mutant_work.mkdir()
        compile_fixture(mutant_work, mutant)
        mutation = execute(mutant_work, "late")
    for case, result in results.items():
        assert result.returncode == 0, f"{case}: {result.stdout}{result.stderr}"
    assert "QA_DMA_TIMEOUT_DRAINED=PASS" in results["late"].stdout
    assert "stack-use-after-return" not in results["late"].stderr
    mutation_text = mutation.stdout + mutation.stderr
    assert mutation.returncode != 0
    assert "stack-use-after-return" in mutation_text
    assert SOURCE.read_bytes() == before
    print(
        "QA_DMA_TIMEOUT_LIFETIME=PASS source_sha256="
        + hashlib.sha256(before).hexdigest()
        + " late_callback_drained=1 mutation_rejected=1"
    )


if __name__ == "__main__":
    main()
