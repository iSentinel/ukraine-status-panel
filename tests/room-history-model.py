#!/usr/bin/env python3
"""Model the M6 24-hour room history ring and gap rules."""

from dataclasses import dataclass
import math

CAPACITY = 288
BUCKET_SECONDS = 300


@dataclass
class Sample:
    epoch: int
    valid: bool
    temperature: float = math.nan
    humidity: float = math.nan


class Model:
    def __init__(self):
        self.samples = []

    def record(self, epoch, temperature, humidity, valid):
        if epoch <= 0:
            return False
        bucket = epoch // BUCKET_SECONDS
        if self.samples and bucket < self.samples[-1].epoch // BUCKET_SECONDS:
            return False
        sample = Sample(
            epoch,
            bool(valid and math.isfinite(temperature)
                 and math.isfinite(humidity)
                 and -327.68 <= temperature <= 327.67
                 and 0.0 <= humidity <= 100.0),
            temperature,
            humidity,
        )
        if self.samples and bucket == self.samples[-1].epoch // BUCKET_SECONDS:
            self.samples[-1] = sample
            return True
        self.samples.append(sample)
        self.samples = self.samples[-CAPACITY:]
        return True

    def segments(self):
        result = []
        current = []
        previous = None
        for sample in self.samples:
            if not sample.valid:
                if current:
                    result.append(current)
                    current = []
                previous = sample.epoch
                continue
            if (previous is not None
                    and sample.epoch - previous > BUCKET_SECONDS * 2):
                if current:
                    result.append(current)
                    current = []
            current.append(sample)
            previous = sample.epoch
        if current:
            result.append(current)
        return result


def main():
    model = Model()
    assert not model.record(0, 20.0, 40.0, True)

    base = 1_800_000_000
    assert model.record(base, math.nan, math.nan, False)
    assert len(model.samples) == 1 and not model.samples[-1].valid

    assert model.record(base + 30, 21.25, 47.5, True)
    assert len(model.samples) == 1 and model.samples[-1].valid

    assert not model.record(base - 300, 20.0, 40.0, True)

    assert model.record(base + 300, math.nan, math.nan, False)
    assert model.record(base + 600, 22.0, 48.0, True)
    segments = model.segments()
    assert [len(item) for item in segments] == [1, 1]

    assert model.record(base + 1800, 22.5, 49.0, True)
    segments = model.segments()
    assert [len(item) for item in segments] == [1, 1, 1]

    fill = Model()
    for index in range(CAPACITY + 50):
        epoch = base + index * BUCKET_SECONDS
        assert fill.record(epoch, 20.0 + index / 100.0, 50.0, True)
    assert len(fill.samples) == CAPACITY
    assert fill.samples[0].epoch == base + 50 * BUCKET_SECONDS
    assert fill.samples[-1].epoch == base + (CAPACITY + 49) * BUCKET_SECONDS

    print(
        "ROOM_HISTORY_MODEL=PASS capacity=288 bucket=300s "
        "same_bucket_replace=1 gaps=1 discontinuity=1 overwrite=1 "
        "untrusted_time_skipped=1 backwards_rejected=1"
    )


if __name__ == "__main__":
    main()
