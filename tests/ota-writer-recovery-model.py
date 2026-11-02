#!/usr/bin/env python3
"""Model writer abort/recovery without restoring consumed authorization."""
from dataclasses import dataclass


@dataclass
class Model:
    candidate: str = 'verified'
    session: str = 'session'
    writer: str = 'idle'
    permit: str | None = 'permit'
    seen: int = 0
    expected: int = 8

    def first_write(self, session: str, permit: str | None) -> int:
        if session != self.session:
            return 403
        if self.candidate != 'verified':
            return 409
        if self.writer not in {'idle', 'aborted'}:
            return 400
        if self.permit is None:
            return 409
        if permit != self.permit:
            return 403
        self.permit = None
        self.writer = 'writing'
        self.seen = 4
        return 202

    def abort(self, session: str) -> int:
        if session != self.session:
            return 403
        if self.candidate != 'verified':
            return 409
        if self.writer == 'aborted':
            return 200
        if self.writer not in {'prepared', 'writing', 'failed'}:
            return 409
        self.permit = None
        self.writer = 'aborted'
        self.seen = 0
        self.expected = 0
        return 200

    def fresh_permit(self) -> None:
        if self.candidate == 'verified' and self.writer == 'aborted':
            self.permit = 'fresh'
            self.expected = 8


def need(ok: bool, message: str) -> None:
    if not ok:
        raise AssertionError(message)


def main() -> None:
    m = Model()
    need(m.first_write('session', 'permit') == 202, 'initial writer did not start')
    need(m.permit is None and m.writer == 'writing' and m.seen == 4,
         'initial permit was not consumed')

    before = (m.candidate, m.writer, m.permit, m.seen, m.expected)
    need(m.abort('wrong') == 403, 'wrong-session abort accepted')
    need((m.candidate, m.writer, m.permit, m.seen, m.expected) == before,
         'wrong-session abort mutated state')

    need(m.abort('session') == 200, 'owner abort failed')
    need(m.candidate == 'verified', 'writer abort destroyed VERIFIED candidate')
    need(m.writer == 'aborted' and m.seen == 0 and m.expected == 0,
         'abort did not clean writer transaction')
    need(m.permit is None, 'abort restored consumed permit')

    need(m.first_write('session', 'permit') == 409,
         'old consumed permit was reusable after abort')
    need(m.writer == 'aborted' and m.candidate == 'verified',
         'failed replay mutated recoverable state')

    m.fresh_permit()
    need(m.first_write('session', 'fresh') == 202,
         'fresh authorization could not restart aborted writer')
    need(m.permit is None and m.writer == 'writing',
         'fresh permit was not one-time')

    m.writer = 'failed'
    m.seen = 4
    m.expected = 8
    need(m.abort('session') == 200, 'failed writer could not be explicitly recovered')
    need(m.writer == 'aborted' and m.candidate == 'verified',
         'failed-writer abort did not preserve candidate')
    need(m.abort('session') == 200, 'repeated abort is not idempotent')

    idle = Model(writer='idle', permit=None)
    need(idle.abort('session') == 409, 'idle writer abort should be rejected')
    finalized = Model(writer='finalized', permit=None)
    need(finalized.abort('session') == 409, 'finalized writer abort should be rejected')

    print('OTA_WRITER_RECOVERY_MODEL=PASS cases=9 candidate_preserved=1 old_permit_replay=blocked fresh_retry=1')


if __name__ == '__main__':
    try:
        main()
    except AssertionError as exc:
        print(f'OTA_WRITER_RECOVERY_MODEL=FAIL {exc}')
        raise SystemExit(1)
