#!/usr/bin/env python3
"""Model the OTA candidate lifecycle and reject illegal transitions."""
from enum import Enum, auto


class State(Enum):
    IDLE = auto()
    MANIFEST_ACCEPTED = auto()
    RECEIVING_IMAGE = auto()
    VERIFIED = auto()
    FAILED = auto()
    ABORTED = auto()


class Session:
    def __init__(self):
        self.state = State.IDLE
        self.seen = 0
        self.expected = 0

    def begin(self, manifest_ok=True, expected=100):
        if self.state is not State.IDLE:
            return False
        if not manifest_ok:
            self.state = State.FAILED
            return False
        self.expected = expected
        self.seen = 0
        self.state = State.MANIFEST_ACCEPTED
        return True

    def chunk(self, size):
        if self.state not in (State.MANIFEST_ACCEPTED, State.RECEIVING_IMAGE):
            return False
        if size <= 0 or self.seen + size > self.expected:
            self.state = State.FAILED
            return False
        self.seen += size
        self.state = State.RECEIVING_IMAGE
        return True

    def finish(self, image_ok=True):
        if self.state not in (State.MANIFEST_ACCEPTED, State.RECEIVING_IMAGE):
            return False
        if self.seen != self.expected or not image_ok:
            self.state = State.FAILED
            return False
        self.state = State.VERIFIED
        return True

    def abort(self):
        self.seen = 0
        self.expected = 0
        self.state = State.ABORTED

    def reset(self):
        self.seen = 0
        self.expected = 0
        self.state = State.IDLE


def main():
    s = Session()
    assert not s.chunk(1) and s.state is State.IDLE
    assert not s.finish() and s.state is State.IDLE
    assert s.begin() and s.state is State.MANIFEST_ACCEPTED
    assert not s.begin() and s.state is State.MANIFEST_ACCEPTED
    assert s.chunk(40) and s.chunk(60)
    assert s.finish() and s.state is State.VERIFIED
    assert not s.chunk(1) and not s.begin()
    s.reset(); assert s.state is State.IDLE
    assert not s.begin(manifest_ok=False) and s.state is State.FAILED
    assert not s.begin(); s.reset()
    assert s.begin(expected=10) and not s.finish() and s.state is State.FAILED
    s.reset(); assert s.begin(expected=10) and not s.chunk(11) and s.state is State.FAILED
    s.reset(); assert s.begin(expected=10) and s.chunk(4); s.abort()
    assert s.state is State.ABORTED and s.seen == 0 and s.expected == 0
    assert not s.begin(); s.reset(); assert s.begin(expected=1)
    print("OTA_CANDIDATE_SESSION_MODEL=PASS transitions=16")


if __name__ == "__main__":
    main()
