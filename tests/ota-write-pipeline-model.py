#!/usr/bin/env python3
"""Model compile-only OTA pipeline ordering and fail-closed cleanup."""
from dataclasses import dataclass


@dataclass
class FakeBackend:
    begin_ok: bool = True
    write_ok: bool = True
    finish_ok: bool = True
    begun: bool = False
    aborted: int = 0
    writes: int = 0
    finished: int = 0

    def begin(self, size: int) -> bool:
        if not self.begin_ok:
            return False
        self.begun = True
        return True

    def write(self, data: bytes) -> bool:
        self.writes += 1
        return self.write_ok

    def finish(self) -> bool:
        self.finished += 1
        return self.finish_ok

    def abort(self) -> None:
        self.aborted += 1
        self.begun = False


@dataclass
class Pipeline:
    backend: FakeBackend | None
    state: str = 'idle'
    failure: str = 'none'
    expected: int = 0
    written: int = 0

    def fail(self, failure: str, abort_backend: bool) -> bool:
        if abort_backend and self.backend is not None:
            self.backend.abort()
        self.failure = failure
        self.state = 'failed'
        return False

    def begin(self, expected: int) -> bool:
        if self.state not in {'idle', 'aborted'}:
            return self.fail('invalid_state', False)
        if self.backend is None:
            return self.fail('backend_unavailable', False)
        if expected == 0:
            return self.fail('invalid_argument', False)
        self.expected = 0
        self.written = 0
        self.failure = 'none'
        if not self.backend.begin(expected):
            return self.fail('backend_begin_failed', True)
        self.expected = expected
        self.state = 'open'
        return True

    def write(self, offset: int, data: bytes) -> bool:
        if self.state not in {'open', 'writing'}:
            return self.fail('invalid_state', True)
        if not data:
            return self.fail('invalid_argument', True)
        if offset != self.written:
            return self.fail('offset_mismatch', True)
        if len(data) > self.expected - self.written:
            return self.fail('size_overflow', True)
        if not self.backend.write(data):
            return self.fail('backend_write_failed', True)
        self.written += len(data)
        self.state = 'writing'
        return True

    def finish(self) -> bool:
        if self.state not in {'open', 'writing'}:
            return self.fail('invalid_state', True)
        if self.written != self.expected:
            return self.fail('size_mismatch', True)
        if not self.backend.finish():
            return self.fail('backend_finish_failed', True)
        self.failure = 'none'
        self.state = 'finalized'
        return True

    def abort(self) -> None:
        if self.backend is not None and self.state in {'open', 'writing', 'failed'}:
            self.backend.abort()
        self.expected = 0
        self.written = 0
        self.failure = 'none'
        self.state = 'aborted'


def need(ok: bool, message: str) -> None:
    if not ok:
        raise AssertionError(message)


def main() -> None:
    p = Pipeline(None)
    need(not p.begin(8) and p.failure == 'backend_unavailable', 'null backend accepted')

    b = FakeBackend(begin_ok=False)
    p = Pipeline(b)
    need(not p.begin(8) and p.failure == 'backend_begin_failed' and b.aborted == 1,
         'begin failure did not abort backend')

    b = FakeBackend()
    p = Pipeline(b)
    need(p.begin(8), 'valid begin failed')
    need(not p.write(1, b'abcd') and p.failure == 'offset_mismatch' and b.writes == 0 and b.aborted == 1,
         'offset mismatch reached backend write or missed abort')

    b = FakeBackend()
    p = Pipeline(b)
    need(p.begin(8), 'valid begin failed')
    need(not p.write(0, b'123456789') and p.failure == 'size_overflow' and b.writes == 0,
         'overflow reached backend write')

    b = FakeBackend(write_ok=False)
    p = Pipeline(b)
    need(p.begin(8), 'valid begin failed')
    need(not p.write(0, b'abcd') and p.failure == 'backend_write_failed' and b.aborted == 1,
         'write failure not fail-closed')

    b = FakeBackend()
    p = Pipeline(b)
    need(p.begin(8) and p.write(0, b'abcd'), 'partial write setup failed')
    need(not p.finish() and p.failure == 'size_mismatch' and b.finished == 0 and b.aborted == 1,
         'early finish reached backend finish')

    b = FakeBackend(finish_ok=False)
    p = Pipeline(b)
    need(p.begin(8) and p.write(0, b'abcd') and p.write(4, b'efgh'), 'finish failure setup failed')
    need(not p.finish() and p.failure == 'backend_finish_failed' and b.aborted == 1,
         'finish failure not fail-closed')

    b = FakeBackend()
    p = Pipeline(b)
    need(p.begin(8) and p.write(0, b'abcd') and p.write(4, b'efgh') and p.finish(),
         'happy path failed')
    need(p.state == 'finalized' and p.written == 8 and b.finished == 1 and b.aborted == 0,
         'happy path accounting wrong')

    b = FakeBackend()
    p = Pipeline(b)
    need(p.begin(8) and p.write(0, b'abcd'), 'abort setup failed')
    p.abort()
    need(p.state == 'aborted' and p.written == 0 and p.expected == 0 and b.aborted == 1,
         'explicit abort cleanup failed')
    need(p.begin(8), 'ABORTED state cannot start a fresh transaction')

    print('OTA_WRITE_PIPELINE_MODEL=PASS cases=9 ordering=1 backend_calls_guarded=1 abort_cleanup=1 fresh_retry=1')


if __name__ == '__main__':
    try:
        main()
    except AssertionError as exc:
        print(f'OTA_WRITE_PIPELINE_MODEL=FAIL {exc}')
        raise SystemExit(1)
