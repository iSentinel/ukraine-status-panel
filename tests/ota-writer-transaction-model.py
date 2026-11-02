#!/usr/bin/env python3
"""Exercise transactional OTA writer semantics without touching flash."""
from dataclasses import dataclass
from enum import Enum, auto
import hashlib


class State(Enum):
    IDLE = auto()
    PREPARED = auto()
    WRITING = auto()
    FINALIZED = auto()
    FAILED = auto()
    ABORTED = auto()


@dataclass
class Writer:
    state: State = State.IDLE
    permit: str = ''
    expected_size: int = 0
    expected_sha: str = ''
    seen: int = 0
    data: bytearray | None = None

    def issue(self, permit: str, verified: bool, session: bool) -> bool:
        if not verified or not session:
            return False
        self.reset()
        self.permit = permit
        return True

    def begin(self, supplied: str, expected_size: int, expected_sha: str,
              verified: bool = True, session: bool = True) -> bool:
        if not verified or not session or not self.permit or supplied != self.permit:
            self.state = State.FAILED
            return False
        if expected_size <= 0 or len(expected_sha) != 64:
            self.state = State.FAILED
            return False
        self.permit = ''
        self.expected_size = expected_size
        self.expected_sha = expected_sha
        self.seen = 0
        self.data = bytearray()
        self.state = State.PREPARED
        return True

    def add(self, offset: int, chunk: bytes) -> bool:
        if self.state not in (State.PREPARED, State.WRITING) or not chunk:
            self.state = State.FAILED
            return False
        if offset != self.seen or self.seen + len(chunk) > self.expected_size:
            self.state = State.FAILED
            return False
        assert self.data is not None
        self.data.extend(chunk)
        self.seen += len(chunk)
        self.state = State.WRITING
        return True

    def finish(self) -> bool:
        if self.state not in (State.PREPARED, State.WRITING):
            self.state = State.FAILED
            return False
        if self.seen != self.expected_size:
            self.state = State.FAILED
            return False
        assert self.data is not None
        if hashlib.sha256(self.data).hexdigest() != self.expected_sha:
            self.state = State.FAILED
            return False
        self.state = State.FINALIZED
        return True

    def abort(self) -> None:
        self.permit = ''
        self.data = None
        self.state = State.ABORTED

    def reset(self) -> None:
        self.state = State.IDLE
        self.permit = ''
        self.expected_size = 0
        self.expected_sha = ''
        self.seen = 0
        self.data = None


def need(ok: bool, message: str) -> None:
    if not ok:
        raise AssertionError(message)


def fresh(payload: bytes = b'abcdef') -> tuple[Writer, str]:
    digest = hashlib.sha256(payload).hexdigest()
    writer = Writer()
    need(writer.issue('permit', True, True), 'permit issue failed')
    need(writer.begin('permit', len(payload), digest), 'prepare failed')
    return writer, digest


def main() -> None:
    writer = Writer()
    need(not writer.issue('x', False, True), 'permit issued before verify')
    need(not writer.issue('x', True, False), 'permit issued without session')
    need(writer.issue('permit', True, True), 'valid permit issue failed')
    need(not writer.begin('wrong', 6, '0' * 64), 'wrong permit accepted')
    need(writer.permit == 'permit', 'wrong permit consumed authorization')
    need(writer.begin('permit', 6, hashlib.sha256(b'abcdef').hexdigest()),
         'valid permit could not recover')
    need(writer.permit == '', 'valid permit was not one-time consumed')
    need(not writer.begin('permit', 6, hashlib.sha256(b'abcdef').hexdigest()),
         'write permit replay succeeded')

    writer, _ = fresh()
    need(writer.add(0, b'abc'), 'first sequential chunk failed')
    need(writer.add(3, b'def'), 'second sequential chunk failed')
    need(writer.finish() and writer.state is State.FINALIZED, 'valid finalize failed')

    writer, _ = fresh()
    need(not writer.add(1, b'a'), 'out-of-order chunk accepted')
    writer, _ = fresh()
    need(not writer.add(0, b'abcdefg'), 'overflow chunk accepted')
    writer, _ = fresh()
    need(writer.add(0, b'abc'), 'short-stream setup failed')
    need(not writer.finish(), 'short stream finalized')
    writer, _ = fresh()
    need(writer.add(0, b'abcdeg'), 'digest-mismatch setup failed')
    need(not writer.finish(), 'digest mismatch finalized')
    writer, _ = fresh()
    writer.abort()
    need(writer.state is State.ABORTED and not writer.add(0, b'a'),
         'aborted transaction accepted data')
    print('OTA_WRITER_TRANSACTION_MODEL=PASS cases=15 replay=blocked ordering=1 digest=1')


if __name__ == '__main__':
    main()
