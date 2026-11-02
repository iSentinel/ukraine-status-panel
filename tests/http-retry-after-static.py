#!/usr/bin/env python3
"""Check that all production HTTP clients honor the shared Retry-After gate."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def require(text: str, token: str, label: str) -> None:
    """Fail with a useful label when a contract token disappears."""
    assert token in text, f"{label}: missing {token!r}"


def ordered(text: str, *tokens: str) -> bool:
    """Return true when all tokens occur in the requested order."""
    position = -1
    for token in tokens:
        position = text.find(token, position + 1)
        if position < 0:
            return False
    return True


def main() -> None:
    """Verify header capture, 429 application, and manual-pending ordering."""
    helper = (ROOT / "include/usp_http_retry_after.h").read_text()
    for token in (
        "MAX_RETRY_AFTER_MS = 900000U",
        "MIN_RETRY_AFTER_MS = 1000U",
        'ascii_iequal(key, "Retry-After")',
        "selected_delay",
        "static_cast<int32_t>(now - target)",
        "sizeof(HeaderCapture) <= 8U",
        "sizeof(Gate) <= 8U",
    ):
        require(helper, token, "helper")

    paths = {
        "weather": ROOT / "components/usp_weather/weather_client.cpp",
        "neptun": ROOT / "components/usp_neptun/neptun_client.cpp",
        "outage": ROOT / "components/usp_outage/outage_client.cpp",
        "svitlobot": ROOT / "components/usp_svitlobot/svitlobot_client.cpp",
    }
    texts = {name: path.read_text() for name, path in paths.items()}
    for name, text in texts.items():
        require(text, "HTTP_EVENT_ON_HEADER", name)
        require(text, "status == 429", name)
        require(text, "retry_after", name)

    weather = texts["weather"]
    require(weather, "search_retry_gate_.ready(now)", "weather-search")
    require(weather, "forecast_retry_gate_.ready(now)", "weather-forecast")
    require(weather, "search_requested_.store(true", "weather-search-requeue")
    assert ordered(
        weather,
        "search_requested_.load",
        "search_retry_gate_.ready(now)",
        "search_requested_.exchange(false",
    ), "weather search must preserve pending while blocked"
    assert ordered(
        weather,
        "refresh_requested_.load",
        "forecast_retry_gate_.ready(now)",
        "refresh_requested_.exchange",
    ), "weather forecast must preserve manual pending while blocked"

    neptun = texts["neptun"]
    assert ordered(
        neptun,
        "refresh_requested_.load",
        "retry_after_gate_.ready(now)",
        "refresh_requested_.exchange",
    ), "NEPTUN manual refresh must remain pending"
    require(
        neptun,
        "if (!this->retry_after_gate_.ready(millis()))",
        "neptun-shared-host-gate",
    )

    outage = texts["outage"]
    assert ordered(
        outage,
        "refresh_requested_.load",
        "retry_after_gate_.ready(now)",
        "refresh_requested_.exchange",
    ), "Outage manual refresh must remain pending"
    require(outage, "GroupHttpContext", "outage-header-context")

    svitlo = texts["svitlobot"]
    assert ordered(
        svitlo,
        "ping_requested_.load",
        "retry_after_gate_.ready(now)",
        "ping_requested_.exchange",
    ), "SvitloBot manual ping must remain pending"
    require(svitlo, "config.event_handler = capture_retry_after", "svitlobot")

    headers = (
        ROOT / "components/usp_weather/weather_client.h",
        ROOT / "components/usp_neptun/neptun_client.h",
        ROOT / "components/usp_outage/outage_client.h",
        ROOT / "components/usp_svitlobot/svitlobot_client.h",
    )
    for path in headers:
        require(path.read_text(), "usp_http_retry_after.h", path.name)

    print(
        "HTTP_RETRY_AFTER_STATIC=PASS clients4 header_capture4 "
        "weather_hosts2 shared_neptun=1 manual_pending4 cap15m=1"
    )


if __name__ == "__main__":
    main()
