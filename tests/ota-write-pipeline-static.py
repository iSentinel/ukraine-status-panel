#!/usr/bin/env python3
"""Validate compile-only OTA write pipeline ordering and isolation."""
from pathlib import Path
from firmware_version_guard import CURRENT_FIRMWARE_MARKER
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
COMP = ROOT / "components/usp_ota_guard"
H = (COMP / "ota_write_pipeline.h").read_text(encoding="utf-8")
CPP = (COMP / "ota_write_pipeline.cpp").read_text(encoding="utf-8")
BACKEND_H = (COMP / "ota_flash_backend.h").read_text(encoding="utf-8")
GUARD = (COMP / "ota_guard.cpp").read_text(encoding="utf-8") + (COMP / "ota_guard.h").read_text(encoding="utf-8")
INIT = (COMP / "__init__.py").read_text(encoding="utf-8")
PROD = (ROOT / "firmware/ukraine-status-panel.yaml").read_text(encoding="utf-8")
PUBLIC = (ROOT / "firmware/ukraine-status-panel-public-first-flash.yaml").read_text(encoding="utf-8")


def need(ok: bool, message: str) -> None:
    if not ok:
        raise AssertionError(message)


def section(start: str, end: str) -> str:
    need(start in CPP and end in CPP, f"missing section: {start} / {end}")
    return CPP.split(start, 1)[1].split(end, 1)[0]


def main() -> None:
    need(CURRENT_FIRMWARE_MARKER in PROD, "wrong production version")
    need(CURRENT_FIRMWARE_MARKER in PUBLIC, "wrong public version")
    need('#include "ota_flash_backend.h"' in H, "pipeline does not use backend contract")
    for token in ("OtaWritePipeline", "WritePipelineState", "WritePipelineFailure",
                  "OFFSET_MISMATCH", "SIZE_OVERFLOW", "SIZE_MISMATCH",
                  "BACKEND_BEGIN_FAILED", "BACKEND_WRITE_FAILED", "BACKEND_FINISH_FAILED"):
        need(token in H + CPP, f"pipeline contract missing: {token}")

    begin = section('bool OtaWritePipeline::begin', 'bool OtaWritePipeline::write')
    need(begin.find('expected_bytes == 0') < begin.find('backend_->begin(expected_bytes)'),
         "argument validation occurs after backend begin")
    need('this->fail_(WritePipelineFailure::BACKEND_BEGIN_FAILED, true)' in begin,
         "backend begin failure is not fail-closed")

    write = section('bool OtaWritePipeline::write', 'bool OtaWritePipeline::finish')
    order = [write.find('offset != this->written_bytes_'),
             write.find('length > this->expected_bytes_ - this->written_bytes_'),
             write.find('backend_->write(data, length)'),
             write.find('this->written_bytes_ += length')]
    need(all(pos >= 0 for pos in order) and order == sorted(order),
         "write validation/backend/accounting order is wrong")
    need('OFFSET_MISMATCH, true' in write and 'SIZE_OVERFLOW, true' in write,
         "stream framing failures do not abort backend")
    need('BACKEND_WRITE_FAILED, true' in write,
         "backend write failure is not fail-closed")

    finish = section('bool OtaWritePipeline::finish', 'void OtaWritePipeline::abort')
    need(finish.find('written_bytes_ != this->expected_bytes_') < finish.find('backend_->finish()'),
         "backend finish occurs before exact-size check")
    need('SIZE_MISMATCH, true' in finish and 'BACKEND_FINISH_FAILED, true' in finish,
         "finish failures are not fail-closed")

    abort = section('void OtaWritePipeline::abort', 'WritePipelineSnapshot OtaWritePipeline::snapshot')
    need('backend_->abort();' in abort and 'clear_counters_();' in abort,
         "pipeline abort does not clean backend/counters")

    need('bool runtime_enabled_{false};' in BACKEND_H,
         "flash backend is no longer hard-disabled")
    for text, label in ((GUARD, 'Web guard'), (INIT, '__init__.py'), (PROD, 'production YAML')):
        need('OtaWritePipeline' not in text and 'ota_write_pipeline' not in text,
             f"pipeline wired into {label}")
    need('usp_ota_guard' not in PUBLIC and 'ota_write_pipeline' not in PUBLIC,
         "pipeline leaked into public first-flash")
    need('/usp/' not in H + CPP and 'AsyncWeb' not in H + CPP,
         "compile-only pipeline exposes Web surface")
    for forbidden in ('esp_ota_set_boot_partition', 'esp_restart', 'App.safe_reboot'):
        need(forbidden not in H + CPP + GUARD, f"activation primitive leaked: {forbidden}")

    instance = re.search(r'(?:^|\n)\s*(?:static\s+)?OtaWritePipeline\s+[A-Za-z_]\w*\s*[;{(]', CPP)
    need(instance is None, "global/static pipeline instance exists")
    print('OTA_WRITE_PIPELINE_STATIC=PASS ordering=1 fail_closed=1 unwired=1 backend_disabled=1 no_activate=1')


if __name__ == '__main__':
    try:
        main()
    except AssertionError as exc:
        print(f'OTA_WRITE_PIPELINE_STATIC=FAIL {exc}', file=sys.stderr)
        raise SystemExit(1)
