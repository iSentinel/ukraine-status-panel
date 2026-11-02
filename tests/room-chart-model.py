#!/usr/bin/env python3
"""Model Room graph segmentation and bounded latest-run rendering."""

from dataclasses import dataclass

MAX_SEGMENTS = 12
BREAK_SECONDS = 600
WIDTH = 420
HEIGHT = 120


@dataclass
class Sample:
    epoch: int
    valid: bool
    temperature: float = 20.0
    humidity: float = 50.0


def runs(samples):
    """Split explicit gaps and timestamp discontinuities without interpolation."""
    result = []
    current = []
    previous = None
    for sample in samples:
        if not sample.valid:
            if current:
                result.append(current)
            current = []
            previous = None
            continue
        if previous is not None and sample.epoch - previous > BREAK_SECONDS:
            if current:
                result.append(current)
            current = []
        current.append(sample)
        previous = sample.epoch
    if current:
        result.append(current)
    return result


def render(samples):
    """Keep at most the newest twelve real runs and scale bounded points."""
    segments = runs(samples)[-MAX_SEGMENTS:]
    valid = [sample for segment in segments for sample in segment]
    if not valid:
        return segments, []
    start, end = valid[0].epoch, valid[-1].epoch
    span = max(1, end - start)
    low = min(sample.temperature for sample in valid)
    high = max(sample.temperature for sample in valid)
    padding = max(0.5, (high - low) * 0.10)
    low, high = low - padding, high + padding
    if high - low < 2.0:
        middle = (low + high) / 2
        low, high = middle - 1.0, middle + 1.0
    points = []
    for sample in valid:
        x = min(WIDTH, (sample.epoch - start) * WIDTH // span)
        temp_y = round((high - sample.temperature) * HEIGHT / (high - low))
        humidity_y = round((100 - sample.humidity) * HEIGHT / 100)
        points.append((x, temp_y, humidity_y))
    return segments, points


def main():
    """Cover continuous, explicit-gap, time-gap and segment-cap cases."""
    base = 1_800_000_000
    contiguous = [
        Sample(base + 300 * index, True, 20 + index, 40 + index)
        for index in range(3)
    ]
    assert [len(item) for item in runs(contiguous)] == [3]
    explicit = (
        contiguous[:2]
        + [Sample(base + 600, False)]
        + [Sample(base + 900, True)]
    )
    assert [len(item) for item in runs(explicit)] == [2, 1]
    jumped = [Sample(base, True), Sample(base + 900, True)]
    assert [len(item) for item in runs(jumped)] == [1, 1]
    many = []
    for index in range(14):
        many += [
            Sample(base + index * 900, True),
            Sample(base + index * 900 + 300, False),
        ]
    segments, points = render(many)
    assert len(segments) == 12 and len(points) == 12
    assert all(
        0 <= x <= WIDTH and 0 <= temp_y <= HEIGHT and 0 <= humidity_y <= HEIGHT
        for x, temp_y, humidity_y in points
    )
    empty_segments, empty_points = render([Sample(base, False)])
    assert empty_segments == [] and empty_points == []
    print(
        "ROOM_CHART_MODEL=PASS contiguous=1 explicit_gap=1 time_gap=1 "
        "latest12=1 bounded=1"
    )


if __name__ == "__main__":
    main()
