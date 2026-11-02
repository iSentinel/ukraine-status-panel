#!/usr/bin/env python3
"""Validate the gzip-based Web UI flash budget and its boundaries."""

import importlib.util
from pathlib import Path
import random
import string

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "scripts/build-web-ui.py"
RUNTIME = ROOT / "firmware/webui/usp-web-ui.js"


def load_build_module():
    """Load the production builder without depending on its filename syntax."""
    spec = importlib.util.spec_from_file_location("usp_build_web_ui", BUILD)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def incompressible_text(length: int) -> str:
    """Create deterministic printable data whose gzip size stays large."""
    rng = random.Random(120240)
    alphabet = string.ascii_letters + string.digits + string.punctuation
    return "".join(rng.choice(alphabet) for _ in range(length))


def main():
    """Prove raw >32 KiB may pass while compressed >16 KiB fails closed."""
    module = load_build_module()
    runtime = RUNTIME.read_text()
    current = module.enforce_budget(runtime)
    assert module.GZIP_BUDGET_BYTES == 16384
    assert current < module.GZIP_BUDGET_BYTES

    compressible = "const pad='" + ("A" * 40000) + "';"
    assert len(compressible.encode()) > 32768
    assert module.enforce_budget(compressible) < module.GZIP_BUDGET_BYTES

    noisy = incompressible_text(26000)
    assert module.gzip_size(noisy) > module.GZIP_BUDGET_BYTES
    try:
        module.enforce_budget(noisy)
    except SystemExit as exc:
        assert "gzip size" in str(exc)
    else:
        raise AssertionError("compressed over-budget fixture was accepted")

    print(
        "WEB_UI_SIZE_BUDGET=PASS "
        f"current_gzip={current} budget={module.GZIP_BUDGET_BYTES} "
        "raw_over_32k_allowed=1 gzip_over_budget_rejected=1"
    )


if __name__ == "__main__":
    main()
