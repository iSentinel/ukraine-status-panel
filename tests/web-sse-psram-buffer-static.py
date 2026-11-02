#!/usr/bin/env python3
"""Guard the ESP-IDF SSE frame buffer against internal-heap regressions."""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
HEADER = (ROOT / 'components/web_server_idf/web_server_idf.h').read_text()
CPP = (ROOT / 'components/web_server_idf/web_server_idf.cpp').read_text()


def need(ok: bool, msg: str) -> None:
    if not ok:
        raise AssertionError(msg)


def main() -> None:
    need('RAMUniquePtr<char[]> event_buffer_' in HEADER, 'SSE buffer is not RAMAllocator-backed')
    need('EVENT_BUFFER_CAPACITY = 8192' in HEADER, 'SSE capacity contract changed')
    need('std::string event_buffer_' not in HEADER, 'SSE buffer regressed to std::string/internal heap')
    need('make_unique_array_for_overwrite(EVENT_BUFFER_CAPACITY)' in CPP,
         'SSE buffer is not allocated through RAMAllocator')
    need('event_buffer_.get()' in CPP, 'SSE send path does not use fixed PSRAM buffer')
    need('event_buffer_.append' not in CPP, 'SSE path still performs dynamic string appends')
    need('event_buffer_.empty' not in CPP and 'event_buffer_.size' not in CPP,
         'SSE path still uses std::string buffer APIs')
    need('Dropping oversized EventSource frame' in CPP, 'oversized SSE frame does not fail closed')
    need('event_buffer_size_ = 0' in CPP, 'SSE buffer reset path missing')
    print('WEB_SSE_PSRAM_BUFFER_STATIC=PASS capacity=8192 dynamic_event_string=0 oversized_drop=1')


if __name__ == '__main__':
    try:
        main()
    except AssertionError as exc:
        print(f'WEB_SSE_PSRAM_BUFFER_STATIC=FAIL {exc}', file=sys.stderr)
        raise SystemExit(1)
